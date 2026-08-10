# OverlayAI Inventory Runtime Regression Matrix

Fecha base: 2026-07-26. Ultima actualizacion: 2026-08-02.

## Estados

- `PASS`: comprobado manualmente o por prueba automatizada en la build actual.
- `PARTIAL`: funciona, pero conserva una limitacion conocida.
- `PENDING`: todavia no se ha comprobado de forma suficiente.
- `N/A`: no corresponde a esa feature.

## Pruebas automatizadas

| Area | Caso | Estado | Evidencia |
|---|---|---:|---|
| Build | `Release|x64` | PASS | Solucion completa compilada; InventoryBridge recompilado y desplegado el 2026-08-02 |
| IPC | Handshake y snapshot | PASS | `OverlayAI.InventoryTests.exe` |
| IPC | Reconexion | PASS | `OverlayAI.InventoryTests.exe` |
| IPC | `entity_quality` serializado | PASS | `OverlayAI.InventoryTests.exe` |
| Persistencia | Storage version 5 | PASS | Slots T/CT de guantes cubiertos por round-trip automatizado; configs anteriores compatibles |
| Frontend simulado | NEW ITEM y coleccion | PASS | `TestPanoramaFrontend.js` |
| Scripts montados | Reveal, coleccion y escala reducida | PASS | `TestPanoramaMountScripts.js` |
| Panorama dentro de CS2 | Diagnostico de solo lectura | PASS | `CUIEngine`, wrapper y root validados en vivo |
| Panorama dentro de CS2 | Montaje tecnico | PASS | Create/destroy, visibilidad y Panic confirmados |
| Panorama dentro de CS2 | Puente de comandos | PASS | Equip/close aceptados por host; polling acotado sin callbacks nativos |

## Music Kits

| Caso | Estado | Observacion |
|---|---:|---|
| Anadir item local | PASS | Se conserva junto al item original |
| Equipar y desequipar | PASS | Restaura el Music ID original |
| Save/Load y preload | PASS | Verificado en varios slots |
| Cambio de controller | PASS | Se resuelve account controller y active controller |
| Controlar un bot | PARTIAL | Funciona; existio un crash no reproducido de forma concluyente |
| Menu principal | PENDING | Depende de Panorama/LoadoutAPI sin raiz external estable |
| Panic y cierre | PASS | La restauracion fue comprobada |
| Servidor oficial | PASS | Comprobado por el usuario con cuenta de prueba |

## Agentes

| Caso | Estado | Observacion |
|---|---:|---|
| Equipar CT | PASS | Modelo y restauracion default comprobados |
| Equipar T | PASS | Modelo Muhlik aplicado y confirmado visualmente |
| Respawn | PASS | Se vuelve a aplicar al nuevo pawn |
| Muerte/espectador | PARTIAL | La resolucion account pawn redujo los fallos previos |
| Controlar un bot | PASS | Comprobado visualmente |
| Cambio entre dos agentes | PASS | Aplicacion y restauracion comprobadas |
| Cobertura del catalogo | PASS | 63/63 definition indexes con modelo runtime y faccion validada |
| Equipar CT | PASS | Modelo Ava aplicado y confirmado visualmente |
| Panic | PASS | El agente default vuelve tras una breve espera |
| Cambio de mapa largo | PENDING | Falta prueba prolongada dedicada |

## Butterfly Knife

| Caso | Estado | Observacion |
|---|---:|---|
| Identidad definition/subclass | PASS | Definition 515 y subclass verificados |
| Calidad especial/estrella | PASS | `m_iEntityQuality = 3`, con restauracion |
| Modelo first person | PASS | Comprobado |
| Modelo world | PASS | Comprobado al soltar y recoger |
| Animaciones Butterfly | PASS | Corregidas y confirmadas |
| Killfeed local | PASS | Jugador y bot muestran icono Butterfly |
| HUD de arma | PARTIAL | Correcto; puede mostrar brevemente el default tras reconstruccion |
| Cambio de arma | PASS | Sin tirones tras optimizar la ruta estable |
| Soltar y recoger | PASS | Mantiene modelo; killfeed corregida |
| Respawn normal | PASS | Reaplicacion validada |
| `mp_restartgame 1` | PARTIAL | Sin crash en la ultima prueba; reaplicacion no instantanea |
| Tienda abierta | PARTIAL | Puede reconstruir temporalmente el icono default |
| Panic/restauracion | PASS | Modelo default restaurado |
| Cuchillo nativo de mapa Workshop | PENDING | Hubo un crash sin dump; causa no aislada |
| Handle invalido/entidad expirada | PASS | Se omite restauracion sobre entidad expirada |
| Sesion prolongada | PENDING | Falta prueba larga despues de los ultimos cambios |

## Cuchillos runtime

| Caso | Estado | Observacion |
|---|---:|---|
| Cobertura del catalogo | PASS | 20/20 definition indexes con subclass, modelo y evento unicos |
| Recursos locales | PASS | Los 20 model paths y nombres de evento existen en la instalacion actual de CS2 |
| Karambit: identidad/modelo | PASS | Definition 507, subclass y modelos first/world confirmados en vivo |
| Karambit: respawn y drop | PASS | Reaplicacion tras ronda y soltar/recoger confirmada por el usuario |
| Karambit: killfeed | PASS | `knife_karambit` observado en eventos reales |
| Karambit: HUD principal | PASS | Icono correcto confirmado visualmente |
| Karambit: menu de compra | PARTIAL | La tienda usa otra representacion de Panorama; queda para una fase posterior |
| Cuchillo al controlar bots | PARTIAL | Caso basico confirmado; restauracion al desactivar y relevo entre pawns corregidos, pendientes de reverificacion visual |
| Matriz visual 20/20 | PASS | Todos los modelos se recorrieron en vivo con una variante representativa; no hubo sustituciones cruzadas ni cuchillos ausentes |
| Paint/wear/seed | PASS | 20/20 combinaciones validas contra el catalogo y aplicadas por runtime; Flip 574, Gut 575 y Huntsman 1117 se corrigieron y confirmaron visualmente el 2026-08-02 |
| StatTrak en SOCache y HUD | PASS | Atributos 80/81, contador y nombre StatTrak confirmados |
| Contador StatTrak fisico | PARTIAL | Falta confirmar visualmente el grabado durante una animacion de inspeccion adecuada |
| Cambios rapidos entre cuchillos | PASS | Cuatro familias consecutivas: 4/4 identidades, acabados y modelos; cero excepciones tras retirar escrituras tardias sobre scene nodes reconstruidos |
| Coleccion oficial acumulada | PASS | Los 20 cuchillos coexistieron y se mostraron simultaneamente en el inventario oficial de Panorama |
| Contador de articulos nuevos | PARTIAL | Las veinte tarjetas aparecen como nuevas; falta confirmar el numero de notificacion desde el menu principal, ya que la pausa en partida no lo muestra |
| Equipar desde Panorama | PASS | Reemplazo para ambos equipos detectado desde el loadout nativo y persistido por IPC |
| Volver al cuchillo default | PASS | Reemplazo nativo CT detectado como unequip; T/CT se mantuvieron independientes y sin vaciado transitorio |

## Guantes

| Caso | Estado | Observacion |
|---|---:|---|
| Catalogo local | PASS | Variantes con definition, paint kit, desgaste e imagen disponibles |
| Equipar T/CT/Ambos | PASS | Slots independientes implementados y cubiertos por pruebas |
| Save/Load y preload | PASS | Persistencia incorporada en storage v5 |
| Snapshot IPC | PASS | Los dos Local ID se publican en el loadout |
| Modelo y acabado runtime | PENDING | Siguiente etapa; requiere aplicar y restaurar `m_EconGloves` de forma segura |

## Panorama

| Caso | Estado | Observacion |
|---|---:|---|
| `PanoramaUIEngine001` | PASS | Interfaz, 18 entradas y `this+0x28` verificados |
| `PanoramaUIClient001` | PASS | Interfaz y 18 entradas de vtable verificadas |
| `CUIEngine` activo | PASS | Resuelto y validado dentro de CS2 |
| Root de Main Menu | PASS | Dos firmas unicas; wrapper y panel validados en vivo |
| Firma `RunScript` | PASS | Unica y llamada desde game thread sin excepcion |
| Root unico frontend | PASS | Implementado en el frontend simulado |
| `pendingRevealItemId` persistente | PASS | Backend versionado y reanudable |
| NEW ITEM dentro de CS2 | PASS | Tarjeta, imagen y datos reales confirmados visualmente |
| Crear/destruir panel | PASS | Ciclo y visibilidad confirmados en Main Menu/pausa |
| Boton `EQUIPAR` | PASS | Equipado real confirmado por snapshot y por el usuario |
| Boton `CONTINUAR` | PASS | Limpia la presentacion pendiente y abre la coleccion local |
| Coleccion tras `CONTINUAR` | PASS | El mismo Music Kit se conserva, se muestra y puede cerrarse |
| Root unico | PASS | Reveal y coleccion reutilizan `OverlayAILocalInventoryRoot` |
| Opcion oficial sin marcador | PASS | Build final activa; funcionamiento general confirmado por el usuario |
| UI reload | PARTIAL | Recuperacion por cambio de root implementada; falta forzarla en vivo |
| Panic sin callbacks residuales | PASS | Desmontaje y retirada limpia del hook confirmados en log |

## Gate antes de aceptar un cambio runtime

1. Compilar `Release|x64`.
2. Ejecutar `OverlayAI.InventoryTests.exe`.
3. Confirmar handshake y snapshot.
4. Repetir las filas afectadas por el cambio.
5. Verificar Panic y restauracion.
6. No avanzar si aparece un crash no explicado en la ruta modificada.
