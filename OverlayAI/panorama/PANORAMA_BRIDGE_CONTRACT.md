# Contrato del puente Panorama de OverlayAI

## Alcance de esta fase

Este prototipo define el frontend local del Inventory Changer y su frontera con un futuro modulo interno. No modifica Steam, Game Coordinator, SOCache ni el inventario real. Tampoco resuelve firmas, indices de vtable o llamadas internas sin verificar.

La auditoria local de `panorama.dll` y `panoramauiclient.dll` encontro `CreateInterface`, pero no una exportacion publica para crear paneles o ejecutar JavaScript. Por eso el adaptador concreto debe permanecer separado hasta verificar una interfaz y su contexto de ejecucion.

La Fase 3B verifico que `PanoramaUIEngine001` y `PanoramaUIClient001` estan registradas en la build del 2026-07-26. Esto no verifica sus vtables. Ademas, `csgo_core/gameinfo.gi` configura `AllowCustomGameUI` en `0`, por lo que el cliente normal rechaza layouts de addons. Vease `PANORAMA_MOUNT_RESEARCH.md`.

## ABI del frontend

- Version: `2`.
- Namespace JavaScript: `OverlayAILocalInventory`.
- Root unico: `OverlayAILocalInventoryRoot`.
- Callback proporcionado por el host: `OverlayAIPanoramaBridgeSend(json)`.
- Entrada host a frontend: `OverlayAILocalInventory.ApplyMessage(json)`.
- Estado del host: `OverlayAILocalInventory.SetBridgeReady(bool)`.
- Ciclo de vida: `Mount()`, `Unmount()` y `Destroy()`.

El callback del host es parte de OverlayAI, no una API atribuida al juego. El host debe instalarlo antes de marcar el puente como disponible.

## Responsabilidades del adaptador interno

1. Resolver una interfaz Panorama verificada desde `CreateInterface` sin asumir nombres ni indices.
2. Ejecutar todas las operaciones de UI en el hilo y contexto de Panorama.
3. Buscar primero `OverlayAILocalInventoryRoot`; nunca crear una segunda instancia.
   El ancla preferida de la vista oficial es `InventoryMainContainer`, con `InventoryMain` como contenido.
4. Cargar los recursos del prototipo y comprobar `GetFrontendAbiVersion() == 2`.
5. Conectar con `\\.\pipe\OverlayAI.Inventory.v1` y reenviar frames JSON con prefijo `uint32` little-endian.
6. Instalar `OverlayAIPanoramaBridgeSend`, llamar `SetBridgeReady(true)` y entregar respuestas mediante `ApplyMessage`.
7. Al descargar, llamar `SetBridgeReady(false)`, luego `Destroy()`, cerrar IPC y liberar el adaptador.

## Estados recomendados

`Unavailable -> InterfaceResolved -> UiContextReady -> PanelMounted -> Connected`

Ante un fallo se retrocede a `Unavailable` y se conserva el Inventory Changer externo. Nunca se debe bloquear el hilo del juego esperando IPC.

## Flujo de Music Kits - Fase 4

La ABI 2 implementa el primer recorrido vertical completo del frontend:

1. Un snapshot con `pending_reveal_item_id` abre la vista `NEW ITEM`.
2. El usuario puede equipar el Music Kit desde la presentacion.
3. `Continuar` envia `inventory.close_reveal` y abre la coleccion sin destruir el root.
4. La coleccion se reconstruye desde cada snapshot y elimina Local IDs duplicados.
5. La seleccion, equipamiento y desequipamiento utilizan exclusivamente `local_id`.
6. Cada respuesta de accion solicita un snapshot nuevo antes de confirmar el estado visual.
7. Una reconexion conserva el identificador de sesion mientras viva el mismo frontend.
8. Una reconstruccion completa genera una sesion nueva para no reutilizar request IDs antiguos.
9. Mientras el panel esta conectado solicita un snapshot cada 1,25 segundos para detectar
   articulos creados desde ImGui; la tarea se cancela al desconectar o destruir el root.

El script `tools/TestPanoramaFrontend.js` simula el host y valida este recorrido sin CS2.
`tools/TestPanoramaMountScripts.js` valida tambien los scripts embebidos que se
ejecutan realmente mediante `RunScript`: root unico, transicion a coleccion,
comandos y escala reducida.
`tools/TestInventoryIpc.ps1` valida handshake, snapshots, reconexion e idempotencia; su
modo `-ExerciseActions` debe utilizarse contra `--inventory-ipc-diagnostics`.

## Coleccion local - Fase 5

La coleccion de la ABI 2 tambien admite:

- Music Kits, armas, cuchillos, guantes y agentes en una lista local unica.
- Busqueda por nombre, tipo, grupo o rareza.
- Filtros por categoria sin alterar el snapshot original.
- Detalles de wear, seed, StatTrak, Souvenir y validez cuando corresponden.
- Duplicado mediante `inventory.duplicate`; el backend valida la fuente y asigna otro Local ID.
- Eliminacion mediante `inventory.remove` con confirmacion visual en dos pasos.
- Equipamiento limitado a Music Kits hasta las fases de aplicacion de las demas categorias.

`TestInventoryIpc.ps1 -ExerciseCollectionActions` crea una copia temporal, comprueba
idempotencia, la elimina y verifica que la coleccion recupere su tamano original.

La validacion en vivo del 2026-07-26 contra `x64/Release/OverlayAI.exe` elevado
partio de 10 articulos, creo una unica copia temporal, rechazo la repeticion del
mismo request ID y volvio a 10 articulos despues de eliminarla. No se modifico el
inventario de Steam ni quedo ningun articulo de prueba persistente.

## Contexto visual comprobado

Los scripts rastreados de CS2 usan `$.GetContextPanel()`, `FindChildInLayoutFile`, `$.CreatePanel`, registro y liberacion de eventos y carga de layouts bajo `file://{resources}`. Esto valida el estilo del frontend y su ciclo de vida, pero no demuestra que un archivo arbitrario pueda montarse desde una ruta externa. El registro o proveedor de recursos sigue siendo responsabilidad del futuro adaptador verificado.

## Fuera de alcance

- Inyeccion, Manual Map o evasion de anti-cheat.
- Escritura en el inventario real o backend de Steam.
- Firmas, hooks o vtables no verificadas.
- Aplicacion en CS2 de armas, cuchillos, guantes o agentes y previews 3D.
