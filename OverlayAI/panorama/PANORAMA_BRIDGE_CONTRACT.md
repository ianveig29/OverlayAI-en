# OverlayAI Panorama Bridge Contract

## Scope of this phase

This prototype defines the local Inventory Changer frontend and its boundary with a future internal module. It does not modify Steam, the Game Coordinator, SOCache or the real inventory. It also does not resolve signatures, vtable indices or internal calls without verification.

The local audit of `panorama.dll` and `panoramauiclient.dll` found `CreateInterface`, but no public export to create panels or execute JavaScript. For this reason the concrete adapter must remain separate until an interface and its execution context are verified.

Phase 3B verified that `PanoramaUIEngine001` and `PanoramaUIClient001` are registered in the 2026-07-26 build. This does not verify their vtables. Additionally, `csgo_core/gameinfo.gi` sets `AllowCustomGameUI` to `0`, so the normal client rejects addon layouts. See `PANORAMA_MOUNT_RESEARCH.md`.

## Frontend ABI

- Version: `2`.
- JavaScript namespace: `OverlayAILocalInventory`.
- Single root: `OverlayAILocalInventoryRoot`.
- Host-provided callback: `OverlayAIPanoramaBridgeSend(json)`.
- Host-to-frontend entry: `OverlayAILocalInventory.ApplyMessage(json)`.
- Host state: `OverlayAILocalInventory.SetBridgeReady(bool)`.
- Lifecycle: `Mount()`, `Unmount()` and `Destroy()`.

The host callback is part of OverlayAI, not an API attributed to the game. The host must install it before marking the bridge as available.

## Internal adapter responsibilities

1. Resolve a verified Panorama interface from `CreateInterface` without assuming names or indices.
2. Execute all UI operations on the Panorama thread and context.
3. Look for `OverlayAILocalInventoryRoot` first; never create a second instance.
   The preferred anchor of the official view is `InventoryMainContainer`, with `InventoryMain` as content.
4. Load the prototype resources and check `GetFrontendAbiVersion() == 2`.
5. Connect to `\\.\pipe\OverlayAI.Inventory.v1` and forward JSON frames with `uint32` little-endian prefix.

## Message contract

All messages are UTF-8 JSON with a `uint32_t` little-endian length prefix (pipe framing).

### Host to frontend

| Type | Purpose |
|---|---|
| `hello` | Handshake: reports `abiVersion`, `hostName` and `protocolVersion`. |
| `itemList` | Full snapshot of the local store: all items with their attributes. |
| `itemAdded` | A single item was added to the store. |
| `itemRemoved` | A single item was removed from the store. |
| `itemUpdated` | A single item's attributes changed. |
| `loadoutChanged` | The active loadout selection changed. |
| `bridgeReady` | The host callback is installed and the bridge is operational. |

### Frontend to host

| Type | Purpose |
|---|---|
| `applyItem` | Request to apply/preview a specific item. |
| `removeItem` | Request to remove a specific item. |
| `equipItem` | Request to equip an item in a loadout slot. |
| `unequipItem` | Request to unequip an item from a loadout slot. |
| `duplicateItem` | Request to duplicate an existing item. |
| `filterChange` | Filter/search parameters changed. |
| `categoryChange` | Active category tab changed. |
| `requestItemList` | Request a full `itemList` refresh. |

## Ownership and lifecycle rules

1. The host owns the pipe, the SOCache boundary and all item data.
2. The frontend owns only the DOM state and user interaction.
3. `Mount()` is idempotent: calling it when already mounted is a no-op.
4. `Unmount()` must release all Panorama event handlers and cancel pending timers.
5. `Destroy()` must remove the root panel if it exists.
6. Reconnection after a pipe drop must not duplicate the root panel; it must call `requestItemList` and rebuild.
7. All JSON keys are camelCase. Enum values are lowercase strings.

## Validation

The frontend must reject any message that:
- Is not valid JSON.
- Does not contain a `type` field.
- Contains an unknown `type`.
- Contains malformed `items` or `item` payloads.

The host must reject any frontend message that:
- Requests an operation on an item id that does not exist in the store.
- Requests an operation not permitted in the current bridge state.
- Contains an unknown `type`.

## Decision

No module is injected, no game files are modified and no `CreatePanoramaUIEngineInternal` is called. The frontend/IPC contract remains valid and the probe is kept as a quick check after updates.

Phases 4 and 5 are completed against the simulated host and the real IPC: NEW ITEM,
continue, collection, filters, search, details, equip, unequip, duplicate,
delete, reconnection and idempotency. When an internal host with a documented
ABI exists, that vertical will be connected without changing the backend or
the frontend.
