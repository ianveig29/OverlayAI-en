# OverlayAI

Local copy ready to be pushed to your private GitHub repository.

Overview
- Project: OverlayAI
- Key source file added: `src/InventoryBridge.cpp`
- Purpose: Check the `-insecure` launch flag and enable (or not) Weapon Skin SOCache per session.

Restriction (required)
In OverlayAI source files `InventoryBridge.cpp` the following restriction must be enforced:

- The global flag `g_weaponSkinSocacheAllowed` MUST only be set to true when the process was launched with the exact token `-insecure` on the command line.
- The check must treat `-insecure` as a separate token (no partial matches such as `-insecurely` allowed).

Acceptable implementation (conceptual):

```cpp
// Pseudocode excerpt - InventoryBridge.cpp must contain logic equivalent to this
bool HasInsecureLaunchFlag() {
	const wchar_t* cmd = GetCommandLineW();
	if (!cmd) return false;
	constexpr const wchar_t flag[] = L"-insecure";
	// find occurrences and ensure token boundaries (preceding and following char are
	// start/end/space/tab/quote). Return true only if a full token is matched.
}

// Only set to true when the function above returns true
g_weaponSkinSocacheAllowed = HasInsecureLaunchFlag();
```

This restriction is mandatory for builds that rely on enabling the Weapon Skin SOCache flag during runtime. Any deviation that allows partial matches or enables the global variable without this token-based check is not acceptable.

How to push to a private GitHub repo (quick)
1. Initialize git locally:
   git init
   git add .
   git commit -m "Add InventoryBridge and READMEs"

2. Create a private repo using GitHub CLI (or via the website):
   gh repo create USER/OverlayAI --private --source=. --remote=origin

3. Push to origin:
   git push -u origin main

If you prefer the website, create the repo there and then:
   git remote add origin https://github.com/YOUR_USER/OverlayAI.git
   git push -u origin main

Notes
- The C++ source provided uses wide-char Windows APIs and writes logs to `overlayai.log`.
- Review and adapt logging policy before using in production.

