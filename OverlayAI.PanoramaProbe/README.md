# OverlayAI Panorama Probe

Herramienta de diagnostico externa. Carga `panorama.dll` y `panoramauiclient.dll` en su propio proceso y consulta nombres conocidos mediante la exportacion `CreateInterface`.

No abre `cs2.exe`, no lee ni escribe su memoria y no invoca metodos de las interfaces devueltas.

Uso:

```powershell
OverlayAI.PanoramaProbe.exe "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64"
```

Un resultado `verified` confirma exclusivamente que las fabricas y versiones nominales siguen registradas. No confirma compatibilidad de vtables ni autoriza llamadas internas.
