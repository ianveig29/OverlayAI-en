# Investigacion de montaje Panorama - Fase 3B

Fecha de comprobacion: 2026-07-26

## Resultado

La build instalada contiene y registra las interfaces de Panorama necesarias como concepto, pero no publica una ABI suficiente para invocar de forma segura la creacion de paneles desde un modulo propio. El montaje real no se implementa en esta fase porque hacerlo exigiria asumir indices de vtable, firmas y reglas de hilo no verificadas.

## Evidencia local

Modulos cargados por el CS2 activo:

- `panorama.dll`
- `panoramauiclient.dll`
- `panorama_text_pango.dll`
- `resourcesystem.dll`
- `client.dll`

Archivos examinados:

- `panorama.dll`: SHA-256 `B8D3E9450596E31310B55BBA9FD58079F12AA5DFD518BE66B6B4567F56132A60`
- `panoramauiclient.dll`: SHA-256 `6CE4EE14C5BFEDCB070886C7247D8B90226358F098E4B4BF6BEB22A19FC5755D`

Exportaciones relevantes:

- Ambos modulos exportan `CreateInterface`.
- `panorama.dll` tambien exporta `CreatePanoramaUIEngineInternal`, sin firma publica.
- `CreatePanel`, `BLoadLayout` y `RunScript` existen internamente, pero no son exportaciones C utilizables directamente.

El probe independiente confirmo sin conectarse a CS2:

- `PanoramaUIEngine001` en `panorama.dll`: disponible, retorno `0`.
- `PanoramaUIClient001` en `panoramauiclient.dll`: disponible, retorno `0`.
- Un nombre de interfaz inexistente devuelve `nullptr` y retorno `1`.

## Restriccion de recursos

La configuracion instalada `csgo_core/gameinfo.gi` contiene:

```text
Panorama
{
    "AllowGlobalPanelContext" "1"
    "AllowCustomGameUI" 0
}
```

Los strings de `panorama.dll` confirman la politica asociada:

- `Error loading %s: Addons cannot add layouts.`
- Los addons, cuando estan permitidos, solo pueden usar `panorama/layout/custom_game/`.

Por tanto, colocar XML, CSS o JavaScript junto al ejecutable no hace que el cliente normal los registre. Modificar los VPK o `gameinfo.gi` no es una solucion aceptable: altera archivos oficiales, requiere reinicio y no satisface el objetivo online y reversible.

## Contexto de menu identificado

La vista oficial de inventario usa un panel con `useglobalcontext="true"`. Los puntos de anclaje observados son:

- `InventoryMainContainer`: contenedor principal y lugar donde CS2 crea su notificacion de equipamiento.
- `InventoryMain`: contenido de listas y categorias.
- `id-navbar-tabs-catagory-btns-container`: navegacion de categorias.

El script oficial registra `ReadyForDisplay`, `UnreadyForDisplay` y `Cancelled`, y libera handlers al ocultarse. Un futuro host debera respetar exactamente ese ciclo y buscar el root de OverlayAI antes de crearlo para evitar duplicados.

## Frontera tecnica

Para montar el panel en el cliente normal todavia faltan datos verificables:

1. Definicion binaria de `IUIEngine`/`IPanoramaUIClient` para esta build.
2. Metodo y firma para obtener el panel de contexto global activo.
3. Metodo y firma para agendar trabajo en el hilo de Panorama.
4. Metodo soportado para registrar recursos propios o ejecutar el frontend embebido.
5. Reglas de ownership para paneles y callbacks durante recargas del menu.

La existencia de `PanoramaUIEngine001` no demuestra ninguno de esos cinco puntos. Invocar una posicion de vtable deducida por ensayo y error no se considera una ABI verificada.

## Decision

No se modifica `OtroInyector`, no se inyecta ningun modulo y no se llama a `CreatePanoramaUIEngineInternal`. El contrato frontend/IPC permanece valido y el probe queda como comprobacion rapida tras actualizaciones.

Las Fases 4 y 5 se completan contra el host simulado y el IPC real: NEW ITEM,
continuar, coleccion, filtros, busqueda, detalles, equipar, desequipar, duplicar,
eliminar, reconexion e idempotencia. Cuando exista un host interno con ABI
documentada, esa vertical se conectara sin cambiar el backend ni el frontend.
