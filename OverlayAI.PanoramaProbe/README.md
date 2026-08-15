# OverlayAI Panorama Probe

External diagnostic tool. Loads `panorama.dll` and `panoramauiclient.dll` in its own process and queries known names via the `CreateInterface` export.

It does not open `cs2.exe`, does not read or write its memory and does not invoke methods of the returned interfaces.

Usage:

```powershell
OverlayAI.PanoramaProbe.exe "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64"
```

A `verified` result exclusively confirms that the named factories and versions remain registered. It does not confirm vtable compatibility or authorize internal calls.
