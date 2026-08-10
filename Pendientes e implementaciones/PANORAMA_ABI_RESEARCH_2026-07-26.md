# OverlayAI - Investigacion ABI de Panorama

Fecha: 2026-07-26
Build local de `client.dll`: timestamp `0x6A5E9F6A`, image size `0x27B4000`.
Build local de `panorama.dll`: timestamp `0x6A500225`, image size `0x5B0000`.

## Regla de seguridad

Ninguna direccion de este documento se considera estable entre actualizaciones. El
runtime debe exigir coincidencia unica de firmas, validar modulos/vtables y cancelar
la operacion completa ante cualquier discrepancia.

## Verificado

- `PanoramaUIEngine001` y `PanoramaUIClient001` existen en la build actual.
- Ambos wrappers exponen 18 entradas ejecutables de vtable (`0..17`).
- `PanoramaUIEngine001[13]`, RVA `0x6D980`, lee el miembro situado en `this+0x28`.
- Dentro de CS2, `PanoramaUIEngine001+0x28` resuelve un `CUIEngine` vivo cuya vtable
  pertenece a `panorama.dll`.
- La firma de `RunScript` aparece exactamente una vez en `panorama.dll`, RVA
  `0xB6700`.
- La firma de `MakeSymbol` aparece exactamente una vez en `panorama.dll`, RVA
  `0xA4C70`.
- Los accesos de atributo usados por el panel fueron verificados en la build actual:
  `GetAttributeString` en `0x8E8` y `SetAttributeString` en `0x918`.
- Dos firmas independientes de `client.dll` resuelven el mismo global de Main Menu:
  `client+0x2408B50`.
- En ejecucion, ese global contiene un wrapper con vtable de `client.dll`.
- El miembro `wrapper+0x8` contiene el panel raiz con vtable de `panorama.dll`.
- Las direcciones anteriores se resuelven por firma y se rechazan si no son unicas,
  ejecutables, alineadas o compatibles con el modulo esperado.

## Corregido durante la investigacion

El primer probe buscaba patrones de `client.dll` dentro de
`panoramauiclient.dll`. Por eso una firma publica parecia desactualizada. Al separar
correctamente los modulos, la firma original y la firma reconstruida del constructor
resultaron unicas y convergieron en `client+0x2408B50`.

Tambien se corrigio el validador de vtables: la tabla reside en memoria de datos de
solo lectura; el que debe ser ejecutable es el puntero al primer metodo. Ademas se
valida que la propia vtable pertenezca al modulo esperado.

## Vertical slice verificado

- Firma de llamada usada para `RunScript`:
  `void(CUIEngine*, CUIPanel*, const char*, const char*, uint64_t)`.
- La tarjeta `NEW ITEM` usa el Music Kit real indicado por
  `pending_reveal_item_id`: imagen, nombre, rareza, estado equipado y Local ID.
- `EQUIPAR` y `CONTINUAR` fueron confirmados visualmente por el usuario y aceptados
  por el host en el log de la sesion.
- Los botones escriben un comando acotado en el atributo `overlayai_cmd` del root.
  C++ lo consulta a 20 Hz, valida el Local ID pendiente y lo limpia antes de encolar
  la accion para el worker IPC.
- No se registran callbacks nativos persistentes en Panorama. Toda llamada a su ABI
  ocurre en `FrameStageNotify`, dentro del hilo del juego.
- Si el root cambia, el montaje se invalida y la tarjeta se vuelve a encolar sobre
  la nueva generacion.
- La opcion persistente `Mostrar NEW ITEM en Panorama` gobierna la presentacion; ya
  no existe un archivo marcador temporal.

## Pendiente de regresion final

- Forzar una reconstruccion completa de Panorama/cambio de mapa y confirmar el
  remontaje automatico en la build final.

La opcion oficial sin marcador temporal quedo habilitada en la build final y el
usuario confirmo que el conjunto continua funcionando correctamente.

## Probe de montaje ejecutado

- `RunScript` se ejecuto una sola vez desde `FrameStageNotify`.
- Se solicito la creacion de un panel con ID fijo y sin callbacks.
- Panic solicito su destruccion desde el mismo hilo antes de retirar el hook.
- CS2 permanecio respondiendo durante el ciclo completo.
- El log confirmo create/destroy y el hook se retiro limpiamente.
- La visibilidad fue confirmada por captura: aparece en el menu de pausa y en el menu
  principal, y permanece oculto durante el gameplay como corresponde al root elegido.
- El marcador temporal del probe fue eliminado al terminar la investigacion.

## Siguiente gate

1. Verificar remontaje tras un reload completo de Panorama o cambio de mapa cuando
   se presente naturalmente durante las pruebas.
2. Repetir Panic y reinyeccion con una presentacion pendiente si se modifica el
   ciclo de vida del bridge.
3. No ampliar a otra categoria sin repetir las filas afectadas de la matriz.

## Rollback requerido

- Una unica accion de UI pendiente a la vez (`create`, `render` o `destroy`).
- No repetir `RunScript` cada frame.
- Ante firma no unica, puntero nulo, vtable ajena o excepcion: no ejecutar.
- En Panic, solicitar destruccion en el hilo del juego antes de retirar el hook.
- Si no existe un frame seguro para destruir, no descargar codigo que conserve
  referencias al panel. La implementacion no registra callbacks nativos persistentes.
