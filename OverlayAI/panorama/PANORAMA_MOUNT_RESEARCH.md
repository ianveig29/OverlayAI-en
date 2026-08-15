# Panorama Mount Research - Phase 3B

Verification date: 2026-07-26

## Result

The installed build contains and registers the necessary Panorama interfaces as a concept, but does not publish a sufficient ABI to safely invoke panel creation from a standalone module. Real mounting is not implemented in this phase because it would require assuming unverified vtable indices, signatures and thread rules.

## Local evidence

Modules loaded by the active CS2:

- `panorama.dll`
- `panoramauiclient.dll`
- `panorama_text_pango.dll`
- `resourcesystem.dll`
- `client.dll`

Files examined:

- `panorama.dll`: SHA-256 `B8D3E9450596E31310B55BBA9FD58079F12AA5DFD518BE66B6B4567F56132A60`
- `panoramauiclient.dll`: SHA-256 `6CE4EE14C5BFEDCB070886C7247D8B90226358F098E4B4BF6BEB22A19FC5755D`

Relevant exports:

- Both modules export `CreateInterface`.
- `panorama.dll` also exports `CreatePanoramaUIEngineInternal`, without a public signature.
- `CreatePanel`, `BLoadLayout` and `RunScript` exist internally, but are not usable C exports.

The standalone probe confirmed without connecting to CS2:

- `PanoramaUIEngine001` in `panorama.dll`: available, returned `0`.
- `PanoramaUIClient001` in `panoramauiclient.dll`: available, returned `0`.
- A non-existent interface name returns `nullptr` and `1`.

## Resource restriction

The installed configuration `csgo_core/gameinfo.gi` contains:

```text
Panorama
{
    "AllowGlobalPanelContext" "1"
    "AllowCustomGameUI" 0
}
```

The strings in `panorama.dll` confirm the associated policy:

- `Error loading %s: Addons cannot add layouts.`
- Addons, when permitted, can only use `panorama/layout/custom_game/`.

Therefore, placing XML, CSS or JavaScript next to the executable does not cause the normal client to register them. Modifying the VPKs or `gameinfo.gi` is not an acceptable solution: it alters official files, requires a restart and does not satisfy the online and reversible goal.

## Identified menu context

The official inventory view uses a panel with `useglobalcontext="true"`. The observed anchor points are:

- `InventoryMainContainer`: main container and where CS2 creates its equipment notification.
- `InventoryMain`: list and category content.
- `id-navbar-tabs-catagory-btns-container`: category navigation.

The official script registers `ReadyForDisplay`, `UnreadyForDisplay` and `Cancelled`, and releases handlers when hidden. A future host must respect exactly this cycle and look for the OverlayAI root before creating it to avoid duplicates.

## Technical boundary

To mount the panel in the normal client, verifiable data is still missing:

1. Binary definition of `IUIEngine`/`IPanoramaUIClient` for this build.
2. Method and signature to obtain the active global context panel.
3. Method and signature to schedule work on the Panorama thread.
4. Supported method to register custom resources or execute the embedded frontend.
5. Ownership rules for panels and callbacks during menu reloads.

The existence of `PanoramaUIEngine001` does not prove any of these five points. Invoking a vtable position deduced by trial and error is not considered a verified ABI.

## Decision

No module is injected, no `CreatePanoramaUIEngineInternal` is called and no game files are modified. The frontend/IPC contract remains valid and the probe is kept as a quick check after updates.

Phases 4 and 5 are completed against the simulated host and the real IPC: NEW ITEM,
continue, collection, filters, search, details, equip, unequip, duplicate,
delete, reconnection and idempotency. When an internal host with a documented
ABI exists, that vertical will be connected without changing the backend or
the frontend.
