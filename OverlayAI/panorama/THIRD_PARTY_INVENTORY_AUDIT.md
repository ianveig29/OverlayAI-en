# Auditoria del Inventory Changer de terceros

Fecha de revision: 2026-07-26

Origen revisado: `C:\Users\ariel\Desktop\Inventory changer`

## Alcance y seguridad

La revision fue exclusivamente estatica. No se compilo, cargo, inyecto ni ejecuto la
DLL incluida, y no se modifico el proceso de CS2.

Microsoft Defender bloqueo las dos copias encontradas de `berserkv2.dll` como
`Trojan:Win32/Kepavll!rfn`. Defender informa `DidThreatExecute: false`,
`IsActive: false` y que la accion de cuarentena termino correctamente. El codigo
propio revisado no contiene conexiones de red, descargas, webhooks, robo de
credenciales ni creacion remota de procesos. Esto no demuestra que la DLL sea
segura ni justifica restaurarla o excluirla del antivirus.

## Arquitectura real

El proyecto es una DLL interna C++23 con hooks de DirectX 11, kiero y MinHook. No
es un frontend de inventario de Panorama.

Su funcion principal consiste en:

1. Crear objetos `CEconItem` con una fabrica interna de `client.dll`.
2. Completar a mano su ID, cuenta, definicion, rareza, calidad y atributos.
3. Insertarlos en el `CGCClientSharedObjectTypeCache` local.
4. Notificar la creacion mediante `CCSPlayerInventory::SOCreated`.
5. Enganchar `EquipItemInLoadout` y `FrameStage` para aplicar la seleccion a las
   armas, cuchillos, guantes y agentes activos.
6. Eliminar los objetos y emitir `SODestroyed` durante el apagado.

Panorama solo se utiliza como cargador de imagenes. El proyecto obtiene
`PanoramaUIEngine001`, recupera el gestor de recursos y carga rutas `s2r://` para
mostrar iconos dentro de ImGui. No crea paneles, no ejecuta JavaScript de Panorama
y no monta layouts en el inventario del menu principal.

## Compatibilidad con el CS2 actual

Se compararon estaticamente las firmas del proyecto con los DLL instalados del
juego. No se consulto memoria del proceso.

Firmas que aun presentan una coincidencia unica:

- Fabrica de `CEconItem`.
- `SetDynamicAttributeValue`.
- Singleton de `CCSInventoryManager`.
- Hook de `LevelInit`.
- Varias funciones auxiliares de entidades y materiales.

Firmas criticas sin coincidencias:

- `CGCClient::FindSOCache`.
- `CGCClientSharedObjectCache::CreateBaseTypeCache`.
- Singleton de `CGCClientSystem`.
- `EquipItemInLoadout`.
- Varias funciones de entidades, materiales y HUD.

Tambien existe al menos una firma con dos coincidencias, por lo que resulta
ambigua. En consecuencia, el proyecto no es utilizable sin rehacer y validar sus
interfaces. Cargarlo sin esos cambios tiene una probabilidad alta de fallo o
crash.

## Supuestos fragiles

El codigo depende de indices de vtable y offsets escritos a mano, entre ellos:

- `CCSInventoryManager::EquipItemInLoadout`: indice 66.
- `CCSInventoryManager::GetLocalInventory`: indice 69.
- `CCSPlayerInventory::SOCreated`: indice 0.
- `CCSPlayerInventory::SODestroyed`: indice 2.
- `CCSPlayerInventory::GetItemInLoadout`: indice 8.
- Cache de objetos: vector en `+0x8`.
- Propietario del inventario: `+0x10`.
- SOCache del inventario: `+0x68`.
- `CGCClient` dentro de `CGCClientSystem`: `+0xB8`.
- `CEconItem`: estructura binaria declarada manualmente.
- `AccessUIEngine`: indice 13.
- `GetResourceManager`: indice 23.

Ninguno de estos valores queda validado por el schema generado por el dumper.
Son ABI internas y pueden cambiar sin que cambie un offset de entidad publico.

## Problemas de calidad detectados

- El hilo principal espera `matchmaking.dll` con un bucle vacio y puede consumir
  un nucleo completo mientras el modulo no exista.
- `hkFrameStage` usa `if (!I::engine && !I::engine->is_in_game())`, condicion que
  puede desreferenciar un puntero nulo. Debe usar una comprobacion separada.
- El desmontaje elimina `hkCreateMaterial` aunque no se observa su instalacion y
  no elimina `hkLevelInit`.
- Se crean tareas `detach()` que pueden sobrevivir a la descarga de la DLL.
- Se llama a logica compleja desde el ciclo de vida iniciado por `DllMain`.
- El stage de aplicacion esta fijado al valor numerico `6`.
- La propiedad y destruccion de los `CEconItem` depende de supuestos no verificados.
- La persistencia JSON es manual y mas fragil que la ya implementada en OverlayAI.
- La creacion automatica fija wear `0.0` sin respetar siempre el rango de la skin.
- Varias estructuras de imagen de Panorama tienen comentarios de layout que no
  coinciden con la alineacion real de sus campos.

## Codigo aprovechable

Se puede reutilizar como referencia, no como copia directa:

- Convenciones de rutas `s2r://panorama/images/econ/...` para iconos.
- Clasificacion visual de armas, cuchillos, guantes y agentes.
- Relacion conceptual entre una seleccion local y su slot de loadout.
- Secuencia conceptual de alta, notificacion, equipamiento y limpieza de objetos.
- Casos de personalizacion que el modelo de datos debe representar: paint kit,
  seed, wear, StatTrak, calidad, rareza y nombre.

No se debe importar directamente:

- La DLL compilada.
- Las firmas, offsets, layouts o indices de vtable.
- El sistema de hooks y su ciclo de vida.
- El parser/persistencia JSON.
- La insercion SOCache antes de una fase dedicada y una autorizacion explicita.

## Decision recomendada

Mantener el proyecto externo actual y el frontend desacoplado de Panorama. Este
repositorio no resuelve el montaje del frontend dentro del inventario de CS2.

Si se autoriza en el futuro una investigacion SOCache, este codigo debe tratarse
solo como una lista de hipotesis. Antes de escribir o enganchar nada habria que:

1. Resolver de nuevo todas las interfaces y funciones criticas.
2. Verificar layouts, propiedad y destructor de `CEconItem`.
3. Verificar cada indice de vtable en la compilacion actual.
4. Implementar un ciclo de vida reversible y sin hilos detached.
5. Probar primero en un proceso/harness aislado y despues en un entorno local.
6. Mantener la persistencia y validacion de OverlayAI como fuente de verdad.

