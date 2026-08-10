# Inventory Changer local: investigacion y arquitectura

Fecha: 2026-07-17

## Estado de la version external

La primera version external funcional incluye un catalogo offline validado de Music Kits, skins de armas, cuchillos, guantes y agentes. Permite buscar, filtrar, crear una coleccion local, configurar wear/seed y variantes permitidas, y guardar todo mediante Configs. Solo los Music Kits se aplican actualmente al cliente; las demas categorias permanecen como catalogo local preparado para una futura integracion interna.

## Alcance obligatorio

- No crear, modificar ni reclamar objetos en Steam.
- No enviar mensajes falsos al Game Coordinator.
- No hacer que los articulos sean visibles para otros jugadores.
- No implementar evasion de VAC ni mecanismos de inyeccion para servidores oficiales.
- Restaurar cualquier override local al desactivar o cerrar OverlayAI.

## Lo que ofrece el proyecto actual

OverlayAI es un proceso externo. Lee y escribe datos con `ReadProcessMemory` y
`WriteProcessMemory`, pero no ejecuta codigo en el hilo de juego de CS2.

`cs2-dumper` aporta dos tipos de informacion utiles:

1. Offsets de campos de clases, por ejemplo `C_EconItemView` y
   `CCSPlayerController_InventoryServices`.
2. Direcciones de interfaces exportadas mediante `CreateInterface`.

El dumper no proporciona constructores de `CEconItem`, funciones para registrar
un shared object, listeners del SOCache ni la funcion interna que ejecuta scripts
de Panorama. Esas funciones no son campos de schema y cambian entre builds.

## Estructuras economicas encontradas

`C_EconItemView` contiene, entre otros:

- Definition index, calidad, nivel y cantidad.
- Item ID, account ID y posicion de inventario.
- Estado de inicializacion y exclusion del SOCache.
- Rareza y calidad sobrescritas.
- Lista de atributos y atributos dinamicos de red.
- Nombre personalizado.

`CCSPlayerController_InventoryServices` contiene:

- Loadout replicado de red.
- Music ID.
- Slots de armas autoritativos del servidor.

Estas estructuras describen objetos o equipamiento ya existentes. Escribirlas no
crea automaticamente una entrada en el inventario principal.

## Como construye CS2 la pantalla de inventario

Los scripts actuales de Panorama llaman a `InventoryAPI.GetInventoryCount()` y
`InventoryAPI.GetInventoryItemIDByIndex()` para obtener los articulos del cache
nativo. Cuando cambia el inventario escuchan el evento
`PanoramaComponent_MyPersona_InventoryUpdated` y vuelven a poblar las listas.

`InventoryAPI` tambien soporta faux item IDs. CS2 los codifica con la mascara
`0xF000000000000000`, el definition index en los bits bajos y el paint kit a
partir del bit 16. Sirven para nombres, imagenes y previsualizaciones 3D, pero no
se agregan por si solos al resultado de `GetInventoryCount()`.

Por lo tanto existen dos formas reales de mostrar articulos falsos:

1. Crear una lista Panorama propia que use faux item IDs.
2. Crear un `CEconItem`, insertarlo en el SOCache local y notificar a sus
   listeners.

La segunda se parece mas al inventario original, pero requiere constructores,
ABI interna, type cache y callbacks no presentes en el dumper. Es mucho mas
fragil y puede corromper memoria si una firma o estructura cambia.

## Limitacion del ejecutable externo

Un proceso externo puede modificar un Music ID o atributos fallback ya
existentes. No puede llamar de forma normal a `InventoryAPI`, crear paneles de
Panorama ni construir `CEconItem` dentro de CS2.

Intentar ejecutar esas funciones mediante un hilo remoto no resuelve el problema:
Panorama y el sistema economico tienen afinidad con el hilo del juego. Ejecutarlos
desde un hilo arbitrario puede bloquear o cerrar CS2.

La opcion Panorama necesita un modulo interno separado que ejecute sus tareas en
el hilo correcto y se comunique con OverlayAI. Ese cambio de arquitectura no debe
hacerse de manera silenciosa ni utilizarse para evadir el anticheat.

## Validacion de articulos

Antes de admitir una entrada, el catalogo debe validar la combinacion completa:

- El definition index existe y pertenece a la categoria elegida.
- El paint kit esta asociado a esa arma o cuchillo.
- El wear respeta `min_float` y `max_float` de la skin.
- StatTrak solo aparece si esa skin admite la variante.
- Souvenir solo aparece si existe esa variante.
- La rareza y calidad proceden del schema; no son valores arbitrarios.
- Los cuchillos conservan calidad de cuchillo y un modelo compatible.
- Stickers, charms y parches respetan cantidad de slots y tipo de articulo.

El catalogo de CSGO-API, generado a partir de archivos del juego, expone
`weapon_id`, `paint_index`, floats, rareza, StatTrak, Souvenir e imagenes. Puede
empaquetarse con OverlayAI para no depender de Internet durante la ejecucion.

## Arquitectura recomendada

### Fase A: catalogo y validador externo

- Empaquetar un catalogo versionado de Music Kits, armas y skins.
- Reemplazar IDs manuales por busqueda y seleccion.
- Guardar presets como articulos tipados, no como offsets sueltos.
- Rechazar combinaciones invalidas antes de tocar memoria.

Esta fase es compatible con el OverlayAI actual y debe hacerse primero.

### Fase B: inventario Panorama local de prueba

- Modulo interno separado y limitado a una prueba offline/insecure.
- Lista Panorama propia alimentada con faux item IDs.
- Music Kits y collectibles primero porque no necesitan wear, seed ni modelo de
  arma.
- Comunicacion con OverlayAI mediante un canal local con mensajes validados.
- Limpieza de paneles y estado al descargar el modulo.

Esta opcion crea una experiencia visual de inventario local sin insertar shared
objects ni tocar Steam.

### Fase C: armas y acabados

- Definition index y paint kit procedentes exclusivamente del catalogo.
- Wear limitado al rango real de la skin.
- Seed solo para acabados que lo utilicen.
- StatTrak y Souvenir tratados como variantes validas, no como un checkbox
  universal.
- Previsualizacion primero; aplicacion al viewmodel despues.

### Fase D: SOCache

No se recomienda para la arquitectura actual. Requeriria localizar y mantener
funciones internas para crear `CEconItem`, obtener el type cache, insertar el
objeto y notificar listeners. Su coste de mantenimiento y riesgo de crash son muy
superiores a la alternativa Panorama.

## Conclusion

La ruta adecuada para OverlayAI es catalogo validado + inventario Panorama propio.
Los offsets del dumper son necesarios para leer y aplicar estados existentes,
pero no son suficientes para crear un inventario falso completo. El prototipo de
Music Kit actual sirve para comprobar el override local; no debe presentarse como
un objeto creado en el inventario de CS2.

## Fuentes

- [Steam Inventory Service](https://partner.steamgames.com/doc/features/inventory)
- [ISteamInventory](https://partner.steamgames.com/doc/api/ISteamInventory)
- [cs2-dumper](https://github.com/a2x/cs2-dumper)
- [GameTracking-CS2: mainmenu_inventory.js](https://github.com/SteamTracking/GameTracking-CS2/blob/master/game/csgo/pak01_dir/panorama/scripts/mainmenu_inventory.js)
- [GameTracking-CS2: itemtile.js](https://github.com/SteamTracking/GameTracking-CS2/blob/master/game/csgo/pak01_dir/panorama/scripts/itemtile.js)
- [Osiris: FauxItemId.h](https://github.com/danielkrupinski/Osiris/blob/master/Source/GameClient/Econ/FauxItemId.h)
- [Osiris: PanoramaUiEngine.h](https://github.com/danielkrupinski/Osiris/blob/master/Source/GameClient/Panorama/PanoramaUiEngine.h)
- [CSGO-API catalog](https://github.com/ByMykel/CSGO-API)
