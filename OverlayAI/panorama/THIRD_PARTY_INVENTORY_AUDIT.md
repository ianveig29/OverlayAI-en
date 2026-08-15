# Third-Party Inventory Changer Audit

Review date: 2026-07-26

Source reviewed: `C:\Users\ariel\Desktop\Inventory changer`

## Scope and safety

The review was exclusively static. The included DLL was not compiled, loaded, injected or executed, and the CS2 process was not modified.

Microsoft Defender blocked both copies of `berserkv2.dll` found as `Trojan:Win32/Kepavll!rfn`. Defender reports `DidThreatExecute: false`, `IsActive: false` and that the quarantine action completed successfully. The reviewed code itself does not contain network connections, downloads, webhooks, credential theft or remote process creation. This does not prove the DLL is safe nor justify restoring it or excluding it from the antivirus.

## Real architecture

The project is an internal C++23 DLL with DirectX 11 hooks, kiero and MinHook. It is not a Panorama inventory frontend.

Its main function consists of:

1. Creating `CEconItem` objects with an internal factory from `client.dll`.
2. Manually filling in their ID, account, definition, rarity, quality and attributes.
3. Inserting them into the local `CGCClientSharedObjectTypeCache`.
4. Notifying creation via `CCSPlayerInventory::SOCreated`.
5. Hooking `EquipItemInLoadout` and `FrameStage` to apply the selection to active weapons, knives, gloves and agents.
6. Removing the objects and emitting `SODestroyed` during shutdown.

Panorama is only used as an image loader. The project obtains `PanoramaUIEngine001`, retrieves the resource manager and loads `s2r://` paths to display icons within ImGui. It does not create panels, does not execute Panorama JavaScript and does not mount layouts in the main menu inventory.

## Compatibility with current CS2

The project's signatures were statically compared with the installed game DLLs. No process memory was queried.

Signatures that still have a unique match:

- `CEconItem` factory.
- `SetDynamicAttributeValue`.
- `CCSInventoryManager` singleton.
- `LevelInit` hook.
- Several entity and material helper functions.

Critical signatures with no matches:

- `CGCClient::FindSOCache`.
- `CGCClientSharedObjectCache::CreateBaseTypeCache`.
- `CGCClientSystem` singleton.
- `EquipItemInLoadout`.
- Several entity, material and HUD functions.

There is also at least one signature with two matches, making it ambiguous. Consequently, the project is not usable without redoing and validating its interfaces. Loading it without these changes has a high probability of failure or crash.

## Fragile assumptions

The code depends on hand-written vtable indices and offsets, including:

- `CCSInventoryManager::EquipItemInLoadout`: index 66.
- `CCSInventoryManager::GetLocalInventory`: index 69.
- `CCSPlayerInventory::SOCreated`: index 0.
- `CCSPlayerInventory::SODestroyed`: index 2.
- `CCSPlayerInventory::GetItemInLoadout`: index 8.
- Object cache: vector at `+0x8`.
- Inventory owner: `+0x10`.
- Inventory SOCache: `+0x68`.
- `CGCClient` inside `CGCClientSystem`: `+0xB8`.
- `CEconItem`: manually declared binary structure.
- `AccessUIEngine`: index 13.
- `GetResourceManager`: index 23.

None of these values are validated by the dumper-generated schema. They are internal ABIs and can change without a public entity offset changing.

## Quality issues detected

- The main thread waits for `matchmaking.dll` with an empty loop and can consume an entire core while the module does not exist.
- `hkFrameStage` uses `if (!I::engine && !I::engine->is_in_game())`, a condition that can dereference a null pointer. It should use a separate check.
- The unmount removes `hkCreateMaterial` even though its installation is not observed and does not remove `hkLevelInit`.
- `detach()` tasks are created that may survive DLL unload.
- Complex logic is called from the lifecycle initiated by `DllMain`.
- The application stage is fixed to the numeric value `6`.
- The ownership and destruction of `CEconItem` objects depends on unverified assumptions.
- JSON persistence is manual and more fragile than the one already implemented in OverlayAI.
- Automatic creation fixes wear to `0.0` without always respecting the skin's range.
- Several Panorama image structures have layout comments that do not match the actual alignment of their fields.

## Reusable code

Can be reused as reference, not as a direct copy:

- `s2r://panorama/images/econ/...` path conventions for icons.
- Visual classification of weapons, knives, gloves and agents.
- Conceptual relationship between a local selection and its loadout slot.
- Conceptual sequence of registration, notification, equipping and cleanup of objects.
- Customization cases that the data model must represent: paint kit, seed, wear, StatTrak, quality, rarity and name.

Should not be imported directly:

- The compiled DLL.
- The signatures, offsets, layouts or vtable indices.
- The hook system and its lifecycle.
- The JSON parser/persistence.
- SOCache insertion before a dedicated phase and explicit authorization.

## Recommended decision

Keep the current external project and the decoupled Panorama frontend. This repository does not solve the frontend mounting inside the CS2 inventory.

If a SOCache investigation is authorized in the future, this code should be treated only as a list of hypotheses. Before writing or hooking anything, one should:

1. Re-resolve all critical interfaces and functions.
2. Verify layouts, ownership and destructor of `CEconItem`.
3. Verify each vtable index in the current build.
4. Implement a reversible lifecycle without detached threads.
5. Test first in an isolated process/harness and then in a local environment.
6. Keep OverlayAI's persistence and validation as the source of truth.
