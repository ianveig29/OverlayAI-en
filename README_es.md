# OverlayAI

Repositorio local de OverlayAI (versión lista para subir a tu repositorio privado en GitHub).

Resumen rápido
- Proyecto: OverlayAI
- Archivo central añadido: `src/InventoryBridge.cpp`
- Propósito: comprobar el flag de lanzamiento `-insecure` y permitir (o no) el acceso a Weapon Skin SOCache por sesión.

Instrucciones para compilar
1. Coloca `src/InventoryBridge.cpp` dentro de tu proyecto C++/Windows (por ejemplo, en un Visual Studio project).
2. Asegúrate de compilar con UNICODE; el código usa API wide (GetCommandLineW, wcsstr).
3. Llama `InitializeInventoryBridge()` desde tu inicialización (por ejemplo DllMain o main) para aplicar la lógica.

Notas de seguridad y diseño
- Por diseño, la variable global `g_weaponSkinSocacheAllowed` se inicializa en `false` y
  solo se habilita si la línea de comando contiene exactamente el token `-insecure`.
- La comprobación distingue tokens completos para evitar coincidencias parciales.
- Logging: se escribe en `overlayai.log` en el directorio de trabajo y también se envía a
  Debug Output (OutputDebugStringW) para depuración.

Estilo y comentarios
- El código en `InventoryBridge.cpp` está abundante y explícitamente comentado en español
  para facilitar auditoría por parte del equipo.

Cómo subir esto a un repositorio privado en GitHub (resumen rápido)
1. Inicializa git si aún no lo hiciste:
   git init
   git add .
   git commit -m "Add InventoryBridge and READMEs"

2. Crea un repositorio privado en GitHub (desde la web o usando la CLI `gh`):
   gh repo create NOMBRE/OverlayAI --private --source=. --remote=origin

3. Empuja los cambios:
   git push -u origin main

Si no puedes usar `gh`, crea el repo en la web, luego enlaza el remoto:
   git remote add origin https://github.com/TU_USUARIO/OverlayAI.git
   git push -u origin main

Licencia y uso
- Este repositorio solo contiene una pieza de ejemplo para control de comportamiento
  basado en línea de comando. Adecúa la licencia según tu proyecto.

Contacto
- Archivos creados automáticamente por la herramienta de integración. Revisa y adapta según tus políticas internas.

