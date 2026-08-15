#include "InventoryCatalog.h"
#include "InventoryIpcServer.h"
#include "InventoryPersistence.h"
#include "InventoryProtocol.h"
#include "InventoryStore.h"
#include "InventoryValidator.h"
#include "PanoramaBridgeContract.h"
#include "third_party/nlohmann/json.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {
    int g_failures = 0;

    struct AgentRuntimeModel {
        int definitionIndex;
        const char* modelPath;
    };

    constexpr AgentRuntimeModel kAgentRuntimeModels[] = {
#include "../OverlayAI.InventoryBridge/AgentModels.inc"
    };

    struct KnifeRuntimeModel {
        int definitionIndex;
        std::uint32_t subclassId;
        const char* modelPath;
        const char* eventName;
    };

    constexpr KnifeRuntimeModel kKnifeRuntimeModels[] = {
#include "../OverlayAI.InventoryBridge/KnifeModels.inc"
    };

    void Check(bool condition, const char* message) {
        if (condition) return;
        ++g_failures;
        std::printf("[FAIL] %s\n", message);
    }

    LocalInventoryItem MakeMusicKit() {
        LocalInventoryItem item;
        for (std::size_t index = 0; index < GetInventoryCatalogSize(); ++index) {
            const InventoryCatalogItem* catalogItem = GetInventoryCatalogItem(index);
            if (!catalogItem || catalogItem->type != LocalInventoryMusicKit) continue;
            item.occupied = true;
            item.type = catalogItem->type;
            item.definitionIndex = catalogItem->definitionIndex;
            item.paintIndex = catalogItem->paintIndex;
            item.wear = catalogItem->minWear;
            strncpy_s(item.displayName, catalogItem->name, _TRUNCATE);
            break;
        }
        return item;
    }

    LocalInventoryItem MakeStatTrakMusicKit(int count) {
        LocalInventoryItem item;
        for (std::size_t index = 0; index < GetInventoryCatalogSize(); ++index) {
            const InventoryCatalogItem* catalogItem = GetInventoryCatalogItem(index);
            if (!catalogItem || catalogItem->type != LocalInventoryMusicKit ||
                !IsInventoryCatalogItemStatTrakAllowed(*catalogItem))
                continue;
            item.occupied = true;
            item.type = catalogItem->type;
            item.definitionIndex = catalogItem->definitionIndex;
            item.paintIndex = catalogItem->paintIndex;
            item.wear = catalogItem->minWear;
            item.statTrak = true;
            item.statTrakCount = count;
            strncpy_s(item.displayName, catalogItem->name, _TRUNCATE);
            break;
        }
        return item;
    }

    LocalInventoryItem MakeCatalogItem(int type, int requiredTeam = LocalInventoryTeamNone) {
        LocalInventoryItem item;
        for (std::size_t index = 0; index < GetInventoryCatalogSize(); ++index) {
            const InventoryCatalogItem* catalogItem = GetInventoryCatalogItem(index);
            if (!catalogItem || catalogItem->type != type ||
                (requiredTeam != LocalInventoryTeamNone &&
                    GetInventoryCatalogItemTeam(*catalogItem) != requiredTeam))
                continue;
            item.occupied = true;
            item.type = catalogItem->type;
            item.definitionIndex = catalogItem->definitionIndex;
            item.paintIndex = catalogItem->paintIndex;
            item.wear = catalogItem->minWear;
            strncpy_s(item.displayName, catalogItem->name, _TRUNCATE);
            break;
        }
        return item;
    }

    LocalInventoryItem MakeWeaponSkin(
        int definitionIndex, bool requireStatTrak = false,
        int paintIndex = 0) {
        LocalInventoryItem item;
        for (std::size_t index = 0; index < GetInventoryCatalogSize(); ++index) {
            const InventoryCatalogItem* catalogItem = GetInventoryCatalogItem(index);
            if (!catalogItem || catalogItem->type != LocalInventoryWeaponSkin ||
                catalogItem->definitionIndex != definitionIndex ||
                (paintIndex > 0 && catalogItem->paintIndex != paintIndex) ||
                (requireStatTrak &&
                    !IsInventoryCatalogItemStatTrakAllowed(*catalogItem)))
                continue;
            item.occupied = true;
            item.type = catalogItem->type;
            item.definitionIndex = catalogItem->definitionIndex;
            item.paintIndex = catalogItem->paintIndex;
            item.wear = catalogItem->minWear;
            strncpy_s(item.displayName, catalogItem->name, _TRUNCATE);
            break;
        }
        return item;
    }

    bool WriteExact(HANDLE pipe, const void* source, DWORD size) {
        const auto* bytes = static_cast<const unsigned char*>(source);
        DWORD completed = 0;
        while (completed < size) {
            DWORD written = 0;
            if (!WriteFile(pipe, bytes + completed, size - completed, &written, nullptr) ||
                written == 0)
                return false;
            completed += written;
        }
        return true;
    }

    bool ReadExact(HANDLE pipe, void* destination, DWORD size) {
        auto* bytes = static_cast<unsigned char*>(destination);
        DWORD completed = 0;
        while (completed < size) {
            DWORD read = 0;
            if (!ReadFile(pipe, bytes + completed, size - completed, &read, nullptr) ||
                read == 0)
                return false;
            completed += read;
        }
        return true;
    }

    void TestIndependentInstancesAndLoadout() {
        InventoryChangerSettings state;
        const LocalInventoryItem candidate = MakeMusicKit();
        Check(candidate.definitionIndex > 0, "El catalogo debe contener un Music Kit.");

        const int firstSlot = AddLocalInventoryItemToStore(state, candidate);
        const int secondSlot = AddLocalInventoryItemToStore(state, candidate);
        Check(firstSlot >= 0 && secondSlot >= 0, "Dos instancias iguales deben poder anadirse.");
        Check(CountLocalInventoryItems(state) == 2, "Las instancias iguales no deben deduplicarse.");

        const LocalItemId firstId = state.items[firstSlot].localId;
        const LocalItemId secondId = state.items[secondSlot].localId;
        Check(firstId != 0 && secondId != 0 && firstId != secondId,
            "Cada instancia debe recibir un Local ID unico.");
        Check(EquipLocalMusicKit(state, firstId), "El Music Kit valido debe poder equiparse.");
        Check(state.enabled && state.loadout.musicKit == firstId,
            "Equipar debe activar el runtime y referenciar el Local ID.");

        const LocalItemId nextBeforeDelete = state.nextLocalId;
        Check(RemoveLocalInventoryItemFromStore(state, firstId), "La instancia equipada debe eliminarse.");
        Check(state.enabled && state.loadout.musicKit == 0,
            "Eliminar el equipado debe limpiar el loadout sin apagar el control maestro.");
        const int thirdSlot = AddLocalInventoryItemToStore(state, candidate);
        Check(thirdSlot >= 0 && state.items[thirdSlot].localId >= nextBeforeDelete,
            "Un Local ID eliminado no debe reutilizarse.");
    }

    void TestInventoryMasterControlIsIndependent() {
        InventoryChangerSettings state;
        state.enabled = true;
        RevalidateLocalInventoryState(state);
        Check(state.enabled,
            "Revalidar una coleccion vacia no debe apagar el control maestro.");

        ClearLocalInventoryStore(state);
        Check(state.enabled,
            "Vaciar la coleccion no debe cambiar la preferencia del runtime.");

        const int slot = AddLocalInventoryItemToStore(state, MakeMusicKit());
        Check(slot >= 0, "La prueba necesita un Music Kit local.");
        if (slot < 0) return;
        Check(EquipLocalMusicKit(state, state.items[slot].localId),
            "El Music Kit de prueba debe poder equiparse.");
        state.enabled = false;
        RevalidateLocalInventoryState(state);
        Check(!state.enabled,
            "Revalidar no debe reactivar un runtime desactivado por el usuario.");
    }

    void TestVersion3RoundTrip() {
        InventoryChangerSettings source;
        source.applyKnivesToControlledBots = true;
        LocalInventoryItem candidate = MakeMusicKit();
        strncpy_s(candidate.customName, "Prueba local", _TRUNCATE);
        const int slot = AddLocalInventoryItemToStore(source, candidate);
        Check(slot >= 0, "La instancia para persistencia debe crearse.");
        const LocalItemId expectedId = source.items[slot].localId;
        Check(EquipLocalMusicKit(source, expectedId), "La instancia debe equiparse antes de guardar.");

        FILE* file = nullptr;
        Check(tmpfile_s(&file) == 0 && file != nullptr, "Debe poder crearse un archivo temporal.");
        if (!file) return;
        Check(SaveInventoryConfig(file, source), "El formato v3 debe guardarse.");
        std::rewind(file);

        InventoryChangerSettings loaded;
        InventoryConfigLoadContext context;
        char line[1024]{};
        while (std::fgets(line, sizeof(line), file))
            (void)TryLoadInventoryConfigLine(loaded, context, line);
        std::fclose(file);
        const InventoryMigrationResult result = FinalizeInventoryConfigLoad(loaded, context);

        Check(context.sourceVersion == kLocalInventoryStorageVersion,
            "La version de inventario debe conservarse.");
        Check(result.assignedLocalIds == 0, "El formato v3 no debe reasignar IDs validos.");
        Check(CountLocalInventoryItems(loaded) == 1, "La coleccion v3 debe restaurarse.");
        Check(loaded.loadout.musicKit == expectedId, "El loadout v3 debe conservar el Local ID.");
        Check(loaded.applyKnivesToControlledBots,
            "La preferencia de cuchillos al controlar bots debe persistir.");
        const LocalInventoryItem* item = FindLocalInventoryItemById(loaded, expectedId);
        Check(item && std::strcmp(item->customName, "Prueba local") == 0,
            "El nombre personalizado debe sobrevivir al guardado.");
    }

    void TestLegacyMigrationAndInvalidState() {
        InventoryChangerSettings state;
        InventoryConfigLoadContext context;
        (void)TryLoadInventoryConfigLine(state, context, "inventory_enabled=1\n");
        (void)TryLoadInventoryConfigLine(state, context, "inventory_selected_slot=4\n");
        (void)TryLoadInventoryConfigLine(state, context, "inventory_equipped_slot=4\n");

        const LocalInventoryItem candidate = MakeMusicKit();
        char legacyLine[256]{};
        sprintf_s(legacyLine, "inventory_item_v2=4,%d,%d,%d,%.6f,0,0,0,%s\n",
            candidate.type, candidate.definitionIndex, candidate.paintIndex,
            candidate.wear, candidate.displayName);
        (void)TryLoadInventoryConfigLine(state, context, legacyLine);
        const InventoryMigrationResult migration = FinalizeInventoryConfigLoad(state, context);
        Check(migration.assignedLocalIds == 1, "La migracion v2 debe asignar un Local ID.");
        Check(state.selectedLocalId != 0 && state.loadout.musicKit == state.selectedLocalId,
            "Los slots antiguos deben migrarse a referencias por Local ID.");

        InventoryChangerSettings invalidState;
        invalidState.items[0].occupied = true;
        invalidState.items[0].type = LocalInventoryWeaponSkin;
        invalidState.items[0].definitionIndex = 999999;
        const InventoryMigrationResult invalidResult = FinalizeLoadedInventoryState(invalidState);
        Check(invalidResult.invalidItems == 1,
            "Un articulo antiguo desconocido debe marcarse sin provocar errores.");
        Check(invalidState.items[0].validity == LocalInventoryOutdated,
            "Un articulo ausente del catalogo debe quedar desactualizado.");
    }

    void TestStatTrakCountValidation() {
        LocalInventoryItem item;
        bool foundCompatibleItem = false;
        for (std::size_t index = 0; index < GetInventoryCatalogSize(); ++index) {
            const InventoryCatalogItem* catalogItem = GetInventoryCatalogItem(index);
            if (!catalogItem ||
                !IsInventoryCatalogItemStatTrakAllowed(*catalogItem))
                continue;
            item.occupied = true;
            item.type = catalogItem->type;
            item.definitionIndex = catalogItem->definitionIndex;
            item.paintIndex = catalogItem->paintIndex;
            item.wear = catalogItem->minWear;
            item.statTrak = true;
            item.statTrakCount = 123456;
            strncpy_s(item.displayName, catalogItem->name, _TRUNCATE);
            foundCompatibleItem = true;
            break;
        }

        Check(foundCompatibleItem,
            "El catalogo debe contener al menos un articulo compatible con StatTrak.");
        if (!foundCompatibleItem) return;

        Check(ValidateLocalInventoryItem(item),
            "Un contador StatTrak elegido por el usuario debe conservarse como valido.");
        item.statTrak = false;
        Check(!ValidateLocalInventoryItem(item),
            "Un contador residual sin StatTrak debe rechazarse.");
        item.statTrakCount = 0;
        Check(ValidateLocalInventoryItem(item),
            "Desactivar StatTrak y limpiar el contador debe volver a ser valido.");
    }

    void TestCatalogDopplerPhases() {
        struct ExpectedPhase {
            int paintIndex;
            const char* suffix;
        };
        constexpr ExpectedPhase expected[] = {
            { 418, "Phase 1" }, { 618, "Phase 2" },
            { 420, "Phase 3" }, { 421, "Phase 4" },
            { 617, "Black Pearl" }, { 415, "Ruby" },
            { 619, "Sapphire" }
        };

        for (const ExpectedPhase& phase : expected) {
            const InventoryCatalogItem* item = FindInventoryCatalogItem(
                LocalInventoryKnife, 515, phase.paintIndex);
            Check(item && std::strstr(item->name, phase.suffix) != nullptr,
                "Cada Doppler Butterfly debe mostrar su fase en el catalogo.");
        }

        const InventoryCatalogItem* emerald = FindInventoryCatalogItem(
            LocalInventoryKnife, 515, 568);
        Check(emerald && std::strstr(emerald->name, "Emerald") != nullptr,
            "Gamma Doppler debe diferenciar la variante Emerald.");

        InventoryChangerSettings state;
        LocalInventoryItem stale;
        stale.occupied = true;
        stale.type = LocalInventoryKnife;
        stale.definitionIndex = 515;
        stale.paintIndex = 618;
        stale.wear = 0.01f;
        strncpy_s(stale.displayName, "Butterfly Knife | Doppler", _TRUNCATE);
        state.items[0] = stale;
        RevalidateLocalInventoryState(state);
        Check(std::strstr(state.items[0].displayName, "Phase 2") != nullptr,
            "La sincronizacion debe actualizar nombres antiguos sin perder el item.");
    }

    void TestMiscInventoryCatalogAndSnapshot() {
        constexpr int types[] = {
            LocalInventoryCollectible,
            LocalInventoryContainer,
            LocalInventoryKey,
            LocalInventorySticker
        };
        InventoryChangerSettings state;
        for (const int type : types) {
            const LocalInventoryItem candidate = MakeCatalogItem(type);
            Check(candidate.definitionIndex > 0,
                "Cada categoria misc debe contener articulos.");
            const int slot = AddLocalInventoryItemToStore(state, candidate);
            Check(slot >= 0,
                "Los articulos misc validos deben poder anadirse.");
            Check(!IsInventoryItemLoadoutSupported(type) &&
                    IsInventoryItemNativeCollectionSupported(type),
                "Los articulos misc deben publicarse sin ocupar loadout.");
        }

        const LocalInventoryItem sticker = MakeCatalogItem(
            LocalInventorySticker);
        const InventoryCatalogItem* stickerCatalog =
            FindInventoryCatalogItem(sticker.type,
                sticker.definitionIndex, sticker.paintIndex);
        Check(sticker.definitionIndex == 1209 && sticker.paintIndex > 0,
            "El sticker debe usar Definition 1209 y un kit secundario.");
        Check(stickerCatalog &&
                !IsInventoryCatalogItemWearCustomizable(*stickerCatalog) &&
                GetInventoryCatalogItemVariantName(*stickerCatalog)[0] != '\0',
            "El kit de sticker no debe exponerse como paint kit editable.");

        const InventoryCatalogItem* regularCoin = FindInventoryCatalogItem(
            LocalInventoryCollectible, 874, 0);
        const InventoryCatalogItem* genuinePin = FindInventoryCatalogItem(
            LocalInventoryCollectible, 6001, 0);
        const InventoryCatalogItem* regularCase = FindInventoryCatalogItem(
            LocalInventoryContainer, 4001, 0);
        const InventoryCatalogItem* regularKey = FindInventoryCatalogItem(
            LocalInventoryKey, 1203, 0);
        Check(regularCoin && regularCoin->quality == 4 &&
                GetInventoryRarityRank(regularCoin->rarity) == 6,
            "Una moneda normal debe conservar calidad Unique y su rareza real.");
        Check(genuinePin && genuinePin->quality == 1,
            "Los pins Genuine deben conservar calidad Genuine.");
        Check(regularCase && regularCase->quality == 4 &&
                GetInventoryRarityRank(regularCase->rarity) == 0,
            "Las cajas comunes no deben publicarse como Unusual.");
        Check(regularKey && regularKey->quality == 4,
            "Las llaves no deben publicarse como Unusual.");
        Check(stickerCatalog && stickerCatalog->quality == 4,
            "Los stickers no deben publicarse como Unusual.");
        Check(GetInventoryRarityRank("Contraband") == 7,
            "Contraband debe usar el valor Immortal definido por items_game.");

        const nlohmann::json snapshot = nlohmann::json::parse(
            BuildInventorySnapshotMessage(state, 92), nullptr, false);
        Check(!snapshot.is_discarded() &&
                snapshot.at("payload").at("items").size() == 4,
            "El snapshot debe transportar todas las categorias misc.");
        if (!snapshot.is_discarded() &&
            snapshot.at("payload").at("items").size() == 4) {
            for (const auto& item : snapshot.at("payload").at("items")) {
                Check(item.contains("rarity_rank"),
                    "Cada articulo debe publicar su rareza numerica.");
                Check(item.contains("quality") &&
                        item.at("quality").get<int>() != 3,
                    "Los articulos misc normales no deben publicarse como Unusual.");
            }
        }
    }

    void TestMusicKitStatTrakFlow() {
        InventoryChangerSettings state;
        const LocalInventoryItem candidate = MakeStatTrakMusicKit(37);
        Check(candidate.definitionIndex > 2 && candidate.statTrak,
            "El catalogo debe ofrecer Music Kits comerciales con StatTrak.");
        const int slot = AddLocalInventoryItemToStore(state, candidate);
        Check(slot >= 0, "El Music Kit StatTrak debe poder anadirse.");
        if (slot < 0) return;

        const LocalItemId localId = state.items[slot].localId;
        Check(EquipLocalMusicKit(state, localId),
            "El Music Kit StatTrak debe poder equiparse.");
        const nlohmann::json snapshot = nlohmann::json::parse(
            BuildInventorySnapshotMessage(state, 91), nullptr, false);
        Check(!snapshot.is_discarded() &&
            snapshot.at("payload").at("items").at(0).at("stattrak") == true &&
            snapshot.at("payload").at("items").at(0).at("stattrak_count") == 37,
            "El snapshot IPC debe conservar StatTrak y el contador de MVPs.");

        FILE* file = nullptr;
        Check(tmpfile_s(&file) == 0 && file != nullptr,
            "Debe poder probarse la persistencia del Music Kit StatTrak.");
        if (!file) return;
        Check(SaveInventoryConfig(file, state),
            "El Music Kit StatTrak debe guardarse en Configs.");
        std::rewind(file);
        InventoryChangerSettings loaded;
        InventoryConfigLoadContext context;
        char line[1024]{};
        while (std::fgets(line, sizeof(line), file))
            (void)TryLoadInventoryConfigLine(loaded, context, line);
        std::fclose(file);
        (void)FinalizeInventoryConfigLoad(loaded, context);
        const LocalInventoryItem* restored =
            FindLocalInventoryItemById(loaded, localId);
        Check(restored && restored->statTrak && restored->statTrakCount == 37,
            "Configs debe restaurar el contador de MVPs sin normalizarlo.");
    }

    void TestWeaponSkinOfflineFlow() {
        InventoryChangerSettings state;
        LocalInventoryItem ak = MakeWeaponSkin(7, true, 801);
        LocalInventoryItem awp = MakeWeaponSkin(9);
        LocalInventoryItem glock = MakeWeaponSkin(4);
        Check(ak.definitionIndex == 7 && awp.definitionIndex == 9 &&
                glock.definitionIndex == 4,
            "El catalogo debe contener skins para AK-47, AWP y Glock-18.");
        const InventoryCatalogItem* akCatalog = FindInventoryCatalogItem(
            LocalInventoryWeaponSkin, 7, 801);
        Check(akCatalog && akCatalog->legacyModel,
            "AK-47 Asiimov debe conservar el indicador de modelo legacy.");
        Check(IsInventoryItemNativeCollectionSupported(
                LocalInventoryWeaponSkin) &&
                IsInventoryItemLoadoutSupported(LocalInventoryWeaponSkin),
            "Las skins deben admitir loadout sin compartir un slot generico.");
        if (ak.definitionIndex != 7 || awp.definitionIndex != 9 ||
            glock.definitionIndex != 4)
            return;

        ak.statTrak = true;
        ak.statTrakCount = 321;
        ak.seed = 777;
        awp.seed = 42;
        glock.seed = 1000;

        const int akSlot = AddLocalInventoryItemToStore(state, ak);
        const int awpSlot = AddLocalInventoryItemToStore(state, awp);
        const int glockSlot = AddLocalInventoryItemToStore(state, glock);
        Check(akSlot >= 0 && awpSlot >= 0 && glockSlot >= 0,
            "Tres skins validas deben poder guardarse en la coleccion local.");
        if (akSlot < 0 || awpSlot < 0 || glockSlot < 0) return;

        Check(EquipLocalInventoryItem(state,
                state.items[akSlot].localId,
                LocalInventoryTeamTerrorist) &&
                EquipLocalInventoryItem(state,
                    state.items[awpSlot].localId,
                    LocalInventoryTeamCounterTerrorist),
            "Las armas deben aceptar el equipo elegido desde el loadout nativo.");
        Check(state.items[akSlot].equippedTeam ==
                LocalInventoryTeamTerrorist &&
                state.items[awpSlot].equippedTeam ==
                LocalInventoryTeamCounterTerrorist,
            "Cada arma debe conservar su equipo sin desplazar otras definiciones.");
        Check(!EquipLocalInventoryItem(state,
                state.items[akSlot].localId,
                LocalInventoryTeamCounterTerrorist) &&
                !EquipLocalInventoryItem(state,
                    state.items[glockSlot].localId,
                    LocalInventoryTeamCounterTerrorist),
            "AK-47 y Glock-18 no deben poder equiparse para CT.");

        Check(CountPendingLocalInventoryReveals(state) == 3,
            "Tres skins nuevas deben formar una unica cola NEW ITEM 1/3.");
        const nlohmann::json snapshot = nlohmann::json::parse(
            BuildInventorySnapshotMessage(state, 93), nullptr, false);
        Check(!snapshot.is_discarded() &&
                snapshot.at("payload").at("items").size() == 3 &&
                snapshot.at("payload").at("pending_reveal_item_ids").size() == 3,
            "El snapshot offline debe transportar las tres skins y su cola.");
        if (snapshot.is_discarded() ||
            snapshot.at("payload").at("items").size() != 3)
            return;

        const auto& akSnapshot = snapshot.at("payload").at("items").at(0);
        Check(akSnapshot.at("type") == LocalInventoryWeaponSkin &&
                akSnapshot.at("stattrak") == true &&
                akSnapshot.at("stattrak_count") == 321 &&
                akSnapshot.at("quality") == 9 &&
                akSnapshot.at("seed") == 777 &&
                akSnapshot.at("legacy_model") == true &&
                akSnapshot.at("equipped_team") ==
                    LocalInventoryTeamTerrorist,
            "Una skin StatTrak debe publicarse como Strange sin perder contador o seed.");
        Check(snapshot.at("payload").at("items").at(1).at("quality") == 4,
            "Una skin normal debe conservar la calidad Unique del catalogo.");

        FILE* file = nullptr;
        Check(tmpfile_s(&file) == 0 && file != nullptr,
            "Debe poder probarse la persistencia offline de armas.");
        if (!file) return;
        Check(SaveInventoryConfig(file, state),
            "La coleccion de armas debe guardarse en Configs.");
        std::rewind(file);
        InventoryChangerSettings loaded;
        InventoryConfigLoadContext context;
        char line[1024]{};
        while (std::fgets(line, sizeof(line), file))
            (void)TryLoadInventoryConfigLine(loaded, context, line);
        std::fclose(file);
        (void)FinalizeInventoryConfigLoad(loaded, context);

        const LocalInventoryItem* restoredAk = FindLocalInventoryItemById(
            loaded, state.items[akSlot].localId);
        Check(restoredAk && restoredAk->statTrak &&
                restoredAk->statTrakCount == 321 && restoredAk->seed == 777 &&
                restoredAk->equippedTeam == LocalInventoryTeamTerrorist,
            "Configs debe restaurar la identidad y StatTrak estatico del arma.");
        Check(CountPendingLocalInventoryReveals(loaded) == 3,
            "Configs debe restaurar la cola NEW ITEM de las tres armas.");
    }

    void TestKnifeAndAgentLoadouts() {
        InventoryChangerSettings state;
        const int firstKnifeSlot = AddLocalInventoryItemToStore(
            state, MakeCatalogItem(LocalInventoryKnife));
        const int secondKnifeSlot = AddLocalInventoryItemToStore(
            state, MakeCatalogItem(LocalInventoryKnife));
        const int terroristAgentSlot = AddLocalInventoryItemToStore(
            state, MakeCatalogItem(LocalInventoryAgent, LocalInventoryTeamTerrorist));
        const int counterTerroristAgentSlot = AddLocalInventoryItemToStore(
            state, MakeCatalogItem(LocalInventoryAgent,
                LocalInventoryTeamCounterTerrorist));
        const int firstGlovesSlot = AddLocalInventoryItemToStore(
            state, MakeCatalogItem(LocalInventoryGloves));
        const int secondGlovesSlot = AddLocalInventoryItemToStore(
            state, MakeCatalogItem(LocalInventoryGloves));
        Check(firstKnifeSlot >= 0 && secondKnifeSlot >= 0 &&
            terroristAgentSlot >= 0 && counterTerroristAgentSlot >= 0 &&
            firstGlovesSlot >= 0 && secondGlovesSlot >= 0,
            "El catalogo debe proporcionar cuchillos, guantes y agentes.");
        if (firstKnifeSlot < 0 || secondKnifeSlot < 0 ||
            terroristAgentSlot < 0 || counterTerroristAgentSlot < 0 ||
            firstGlovesSlot < 0 || secondGlovesSlot < 0)
            return;

        const LocalItemId firstKnife = state.items[firstKnifeSlot].localId;
        const LocalItemId secondKnife = state.items[secondKnifeSlot].localId;
        const LocalItemId terroristAgent = state.items[terroristAgentSlot].localId;
        const LocalItemId counterTerroristAgent =
            state.items[counterTerroristAgentSlot].localId;
        const LocalItemId firstGloves = state.items[firstGlovesSlot].localId;
        const LocalItemId secondGloves = state.items[secondGlovesSlot].localId;

        Check(EquipLocalInventoryItem(state, firstKnife, LocalInventoryTeamBoth),
            "Un cuchillo debe poder equiparse para ambos equipos.");
        Check(state.loadout.terroristKnife == firstKnife &&
            state.loadout.counterTerroristKnife == firstKnife &&
            state.items[firstKnifeSlot].equippedTeam == LocalInventoryTeamBoth,
            "El cuchillo para ambos debe ocupar los dos slots.");
        Check(EquipLocalInventoryItem(
            state, secondKnife, LocalInventoryTeamTerrorist),
            "El cuchillo terrorista debe poder reemplazarse de forma independiente.");
        Check(state.loadout.terroristKnife == secondKnife &&
            state.loadout.counterTerroristKnife == firstKnife &&
            state.items[firstKnifeSlot].equippedTeam ==
                LocalInventoryTeamCounterTerrorist &&
            state.items[secondKnifeSlot].equippedTeam == LocalInventoryTeamTerrorist,
            "Reemplazar T no debe modificar el cuchillo CT.");

        Check(!EquipLocalInventoryItem(
            state, terroristAgent, LocalInventoryTeamCounterTerrorist),
            "Un agente T debe rechazarse en el slot CT.");
        Check(EquipLocalInventoryItem(
            state, terroristAgent, LocalInventoryTeamTerrorist) &&
            EquipLocalInventoryItem(state, counterTerroristAgent,
                LocalInventoryTeamCounterTerrorist),
            "Cada agente debe equiparse en su faccion valida.");
        Check(EquipLocalInventoryItem(state, firstGloves, LocalInventoryTeamBoth),
            "Unos guantes deben poder equiparse para ambos equipos.");
        Check(EquipLocalInventoryItem(
            state, secondGloves, LocalInventoryTeamTerrorist),
            "Los guantes T deben poder reemplazarse de forma independiente.");
        Check(state.loadout.terroristGloves == secondGloves &&
            state.loadout.counterTerroristGloves == firstGloves &&
            state.items[firstGlovesSlot].equippedTeam ==
                LocalInventoryTeamCounterTerrorist &&
            state.items[secondGlovesSlot].equippedTeam ==
                LocalInventoryTeamTerrorist,
            "Reemplazar guantes T no debe modificar los guantes CT.");

        FILE* file = nullptr;
        Check(tmpfile_s(&file) == 0 && file != nullptr,
            "Debe poder crearse la config temporal de loadouts.");
        if (!file) return;
        Check(SaveInventoryConfig(file, state),
            "Los loadouts de cuchillos y agentes deben guardarse.");
        std::rewind(file);
        InventoryChangerSettings loaded;
        InventoryConfigLoadContext context;
        char line[1024]{};
        while (std::fgets(line, sizeof(line), file))
            (void)TryLoadInventoryConfigLine(loaded, context, line);
        std::fclose(file);
        (void)FinalizeInventoryConfigLoad(loaded, context);
        Check(loaded.loadout.terroristKnife == secondKnife &&
            loaded.loadout.counterTerroristKnife == firstKnife &&
            loaded.loadout.terroristGloves == secondGloves &&
            loaded.loadout.counterTerroristGloves == firstGloves &&
            loaded.loadout.terroristAgent == terroristAgent &&
            loaded.loadout.counterTerroristAgent == counterTerroristAgent,
            "La config v5 debe restaurar cuchillos, guantes y agentes.");
    }

    void TestAgentRuntimeCoverage() {
        std::size_t catalogAgents = 0;
        std::size_t mappedAgents = 0;
        for (std::size_t index = 0; index < GetInventoryCatalogSize(); ++index) {
            const InventoryCatalogItem* item = GetInventoryCatalogItem(index);
            if (!item || item->type != LocalInventoryAgent) continue;
            ++catalogAgents;
            const int team = GetInventoryCatalogItemTeam(*item);
            Check(team == LocalInventoryTeamTerrorist ||
                team == LocalInventoryTeamCounterTerrorist,
                "Cada agente debe pertenecer a una faccion valida.");

            int matches = 0;
            for (const AgentRuntimeModel& model : kAgentRuntimeModels) {
                if (model.definitionIndex != item->definitionIndex) continue;
                ++matches;
                Check(model.modelPath &&
                    std::strncmp(model.modelPath, "agents/models/", 14) == 0 &&
                    std::strstr(model.modelPath, ".vmdl") != nullptr,
                    "Cada agente runtime debe tener un model path valido.");
            }
            Check(matches == 1,
                "Cada agente del catalogo debe tener exactamente un modelo runtime.");
            if (matches == 1) ++mappedAgents;
        }

        Check(catalogAgents > 0 && mappedAgents == catalogAgents,
            "La tabla runtime debe cubrir todos los agentes del catalogo.");
        Check(mappedAgents == _countof(kAgentRuntimeModels),
            "La tabla runtime no debe contener agentes duplicados u obsoletos.");
    }

    void TestKnifeRuntimeTable() {
        std::size_t catalogKnifeDefinitions = 0;
        for (std::size_t catalogIndex = 0;
            catalogIndex < GetInventoryCatalogSize(); ++catalogIndex) {
            const InventoryCatalogItem* item =
                GetInventoryCatalogItem(catalogIndex);
            if (!item || item->type != LocalInventoryKnife) continue;

            bool seenEarlier = false;
            for (std::size_t earlier = 0; earlier < catalogIndex; ++earlier) {
                const InventoryCatalogItem* previous =
                    GetInventoryCatalogItem(earlier);
                if (previous && previous->type == LocalInventoryKnife &&
                    previous->definitionIndex == item->definitionIndex) {
                    seenEarlier = true;
                    break;
                }
            }
            if (seenEarlier) continue;
            ++catalogKnifeDefinitions;

            int runtimeMatches = 0;
            for (const KnifeRuntimeModel& model : kKnifeRuntimeModels) {
                if (model.definitionIndex == item->definitionIndex)
                    ++runtimeMatches;
            }
            Check(runtimeMatches == 1,
                "Cada cuchillo del catalogo debe tener exactamente un modelo runtime.");
        }

        for (std::size_t index = 0; index < _countof(kKnifeRuntimeModels); ++index) {
            const KnifeRuntimeModel& model = kKnifeRuntimeModels[index];
            Check(FindInventoryCatalogItem(LocalInventoryKnife,
                model.definitionIndex, 0) != nullptr,
                "Cada cuchillo runtime debe existir en el catalogo.");
            Check(model.subclassId != 0 && model.modelPath &&
                std::strncmp(model.modelPath, "weapons/models/knife/", 21) == 0 &&
                std::strstr(model.modelPath, ".vmdl") != nullptr,
                "Cada cuchillo runtime debe tener subclass y modelo validos.");
            Check(model.eventName &&
                (std::strncmp(model.eventName, "knife_", 6) == 0 ||
                 (model.definitionIndex == 500 &&
                  std::strcmp(model.eventName, "bayonet") == 0)),
                "Cada cuchillo runtime debe declarar su identidad de killfeed.");
            for (std::size_t other = index + 1;
                other < _countof(kKnifeRuntimeModels); ++other) {
                Check(model.definitionIndex !=
                    kKnifeRuntimeModels[other].definitionIndex,
                    "La tabla runtime no debe repetir definition indexes.");
            }
        }
        Check(catalogKnifeDefinitions == _countof(kKnifeRuntimeModels),
            "La tabla runtime debe cubrir todas las definiciones de cuchillo del catalogo.");
    }

    void TestProtocolValidationAndSnapshot() {
        InventoryProtocolCommand command;
        InventoryProtocolError error;
        const std::string hello = R"({"protocol_version":1,"message_type":"client.hello","request_id":1,"payload":{"client_session_id":"inventory-test"}})";
        Check(ParseInventoryProtocolCommand(hello, command, error),
            "client.hello valido debe aceptarse.");
        Check(command.type == InventoryProtocolCommandType::Hello &&
            command.clientSessionId == "inventory-test",
            "El saludo debe conservar la sesion del cliente.");

        const std::string extraField = R"({"protocol_version":1,"message_type":"inventory.request_refresh","request_id":2,"payload":{},"extra":1})";
        Check(!ParseInventoryProtocolCommand(extraField, command, error) &&
            error.code == "invalid_envelope",
            "Los campos arbitrarios del sobre deben rechazarse.");

        const std::string invalidPayload = R"({"protocol_version":1,"message_type":"inventory.equip","request_id":3,"payload":{"local_id":1,"pointer":1234}})";
        Check(!ParseInventoryProtocolCommand(invalidPayload, command, error) &&
            error.code == "invalid_payload",
            "Los campos arbitrarios del payload deben rechazarse.");

        const std::string duplicate = R"({"protocol_version":1,"message_type":"inventory.duplicate","request_id":4,"payload":{"local_id":41}})";
        Check(ParseInventoryProtocolCommand(duplicate, command, error) &&
            command.type == InventoryProtocolCommandType::Duplicate && command.localId == 41,
            "inventory.duplicate debe aceptar solamente un Local ID valido.");

        const std::string teamEquip = R"({"protocol_version":1,"message_type":"inventory.equip","request_id":5,"payload":{"local_id":41,"team":2}})";
        Check(ParseInventoryProtocolCommand(teamEquip, command, error) &&
            command.type == InventoryProtocolCommandType::Equip &&
            command.localId == 41 &&
            command.team == LocalInventoryTeamCounterTerrorist,
            "inventory.equip debe aceptar un equipo de loadout explicito.");

        InventoryChangerSettings state;
        const int slot = AddLocalInventoryItemToStore(state, MakeMusicKit());
        Check(slot >= 0, "El snapshot necesita una instancia local.");
        InventoryRuntimeOffsets runtimeOffsets;
        runtimeOffsets.entityList = 0x1000;
        runtimeOffsets.localPlayerController = 0x1100;
        runtimeOffsets.localPlayerPawn = 0x1234;
        runtimeOffsets.playerPawnHandle = 0x30;
        runtimeOffsets.controllingBot = 0x32;
        runtimeOffsets.hasFemaleVoice = 0x34;
        runtimeOffsets.teamNumber = 0x40;
        runtimeOffsets.lifeState = 0x44;
        runtimeOffsets.lastSpawnTimeIndex = 0x50;
        runtimeOffsets.gameSceneNode = 0x60;
        runtimeOffsets.modelState = 0x70;
        runtimeOffsets.modelName = 0x80;
        runtimeOffsets.entityQuality = 0x90;
        runtimeOffsets.econGloves = 0xA0;
        runtimeOffsets.needToReApplyGloves = 0xA8;
        const std::string snapshot = BuildInventorySnapshotMessage(
            state, 5, &runtimeOffsets);
        const nlohmann::json parsed = nlohmann::json::parse(snapshot, nullptr, false);
        Check(!parsed.is_discarded() && parsed.at("protocol_version") == 1 &&
            parsed.at("message_type") == "inventory.snapshot" &&
            parsed.at("payload").at("items").size() == 1 &&
            parsed.at("payload").at("runtime_offsets").at(
                "local_player_pawn") == 0x1234 &&
            parsed.at("payload").at("runtime_offsets").at(
                "local_player_controller") == 0x1100 &&
            parsed.at("payload").at("runtime_offsets").at(
                "player_pawn_handle") == 0x30 &&
            parsed.at("payload").at("runtime_offsets").at(
                "controlling_bot") == 0x32 &&
            parsed.at("payload").at("runtime_offsets").at(
                "has_female_voice") == 0x34 &&
            parsed.at("payload").at("runtime_offsets").at("model_name") == 0x80 &&
            parsed.at("payload").at("runtime_offsets").at("entity_quality") == 0x90 &&
            parsed.at("payload").at("runtime_offsets").at("econ_gloves") == 0xA0 &&
            parsed.at("payload").at("runtime_offsets").at(
                "need_to_reapply_gloves") == 0xA8,
            "El snapshot debe ser JSON versionado y contener la coleccion.");
        Check(parsed.at("payload").at("apply_knives_to_controlled_bots") == false,
            "El snapshot debe usar comportamiento realista por defecto.");
    }

    void TestPanoramaContractVersion() {
        Check(OverlayAIPanoramaContract::kInventoryProtocolVersion == kInventoryProtocolVersion,
            "Panorama y el servidor deben usar la misma version de protocolo.");
        Check(OverlayAIPanoramaContract::IsFrontendCompatible(2) &&
            !OverlayAIPanoramaContract::IsFrontendCompatible(1),
            "La ABI Panorama debe rechazar versiones desconocidas.");
    }

    void TestPhase4MusicKitFlow() {
        InventoryChangerSettings state;
        const int firstSlot = AddLocalInventoryItemToStore(state, MakeMusicKit());
        const int secondSlot = AddLocalInventoryItemToStore(state, MakeMusicKit());
        Check(firstSlot >= 0 && secondSlot >= 0,
            "La Fase 4 necesita dos Music Kits locales.");
        if (firstSlot < 0 || secondSlot < 0) return;

        const LocalItemId firstId = state.items[firstSlot].localId;
        const LocalItemId secondId = state.items[secondSlot].localId;
        Check(state.pendingRevealItemId == firstId &&
            CountPendingLocalInventoryReveals(state) == 2 &&
            IsLocalInventoryRevealPending(state, secondId),
            "Los articulos nuevos deben acumularse en una sola cola de reveal.");
        Check(EquipLocalMusicKit(state, secondId),
            "El articulo pendiente debe poder equiparse.");

        nlohmann::json snapshot = nlohmann::json::parse(
            BuildInventorySnapshotMessage(state, 50), nullptr, false);
        Check(!snapshot.is_discarded() &&
            snapshot.at("payload").at("pending_reveal_item_id") == firstId &&
            snapshot.at("payload").at("pending_reveal_item_ids").size() == 2 &&
            snapshot.at("payload").at("loadout").at("music_kit") == secondId,
            "El snapshot debe reflejar reveal y loadout de la Fase 4.");

        ClearPendingLocalInventoryReveals(state);
        UnequipLocalMusicKit(state);
        snapshot = nlohmann::json::parse(
            BuildInventorySnapshotMessage(state, 51), nullptr, false);
        Check(snapshot.at("payload").at("pending_reveal_item_id") == 0 &&
            snapshot.at("payload").at("loadout").at("music_kit") == 0 &&
            snapshot.at("payload").at("items").size() == 2,
            "Continuar y desequipar no deben eliminar ni duplicar la coleccion.");
        Check(FindLocalInventoryItemById(state, firstId) != nullptr &&
            FindLocalInventoryItemById(state, secondId) != nullptr,
            "La coleccion debe conservar identidades independientes.");
    }

    void TestManualRevealQueue() {
        InventoryChangerSettings state;
        state.queueRevealWhenUnavailable = false;
        const int slot = AddLocalInventoryItemToStore(
            state, MakeCatalogItem(LocalInventoryKnife));
        Check(slot >= 0, "La prueba manual necesita un cuchillo local.");
        if (slot < 0) return;

        const LocalItemId localId = state.items[slot].localId;
        Check(state.pendingRevealItemId == kInvalidLocalItemId,
            "Desactivar el reveal automatico no debe encolar articulos nuevos.");
        Check(QueueLocalInventoryReveal(state, localId) &&
            state.pendingRevealItemId == localId,
            "Una presentacion manual debe funcionar aunque el modo automatico este desactivado.");
        Check(!QueueLocalInventoryReveal(state, localId + 1000),
            "No debe encolarse un Local ID inexistente.");
    }

    void TestMusicKitApplyRevision() {
        InventoryChangerSettings state;
        const std::string snapshot = BuildInventorySnapshotMessage(
            state, 73, nullptr, 42);
        const nlohmann::json parsed = nlohmann::json::parse(
            snapshot, nullptr, false);
        Check(!parsed.is_discarded() &&
            parsed.at("payload").at("music_kit_apply_revision") == 42,
            "El snapshot debe transportar la revision de aplicacion del Music Kit.");
    }

    void TestNamedPipeTransport() {
        InventoryIpcServer server;
        Check(server.Start(), "El servidor IPC autocontenido debe iniciar.");
        if (!server.GetStatus().running) return;

        const std::string hello = R"({"protocol_version":1,"message_type":"client.hello","request_id":700,"payload":{"client_session_id":"pipe-test"}})";
        std::string clientResponse;
        bool clientSucceeded = false;
        std::thread client([&] {
            if (!WaitNamedPipeW(kInventoryIpcPipeName, 3000)) return;
            HANDLE pipe = CreateFileW(kInventoryIpcPipeName,
                GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) return;

            const uint32_t requestSize = static_cast<uint32_t>(hello.size());
            if (!WriteExact(pipe, &requestSize, sizeof(requestSize)) ||
                !WriteExact(pipe, hello.data(), requestSize)) {
                CloseHandle(pipe);
                return;
            }

            uint32_t responseSize = 0;
            if (!ReadExact(pipe, &responseSize, sizeof(responseSize)) ||
                responseSize == 0 || responseSize > kInventoryProtocolMaxFrameBytes) {
                CloseHandle(pipe);
                return;
            }
            clientResponse.resize(responseSize);
            clientSucceeded = ReadExact(pipe, clientResponse.data(), responseSize);
            CloseHandle(pipe);
        });

        InventoryIpcInboundFrame frame;
        bool received = false;
        for (int attempt = 0; attempt < 3000 && !received; ++attempt) {
            received = server.TryReceive(frame);
            if (!received) Sleep(1);
        }
        Check(received && frame.payload == hello,
            "El servidor debe recibir el frame JSON exacto.");

        InventoryChangerSettings state;
        (void)AddLocalInventoryItemToStore(state, MakeMusicKit());
        if (received) {
            Check(server.Send(frame.connectionId,
                BuildInventorySnapshotMessage(state, 700)),
                "El servidor debe aceptar la respuesta del snapshot.");
        }
        client.join();
        server.Stop();

        const nlohmann::json response = nlohmann::json::parse(
            clientResponse, nullptr, false);
        Check(clientSucceeded && !response.is_discarded() &&
            response.at("message_type") == "inventory.snapshot" &&
            response.at("request_id") == 700 &&
            response.at("payload").at("items").size() == 1,
            "El framing real del named pipe debe conservar el snapshot completo.");
        Check(!server.GetStatus().running,
            "El servidor IPC debe liberar su hilo y pipe al detenerse.");
    }
}

int main() {
#define RUN_TEST(test) do { \
        std::printf("[RUN] %s\n", #test); \
        std::fflush(stdout); \
        test(); \
    } while (false)
    RUN_TEST(TestIndependentInstancesAndLoadout);
    RUN_TEST(TestInventoryMasterControlIsIndependent);
    RUN_TEST(TestVersion3RoundTrip);
    RUN_TEST(TestLegacyMigrationAndInvalidState);
    RUN_TEST(TestStatTrakCountValidation);
    RUN_TEST(TestCatalogDopplerPhases);
    RUN_TEST(TestMiscInventoryCatalogAndSnapshot);
    RUN_TEST(TestMusicKitStatTrakFlow);
    RUN_TEST(TestWeaponSkinOfflineFlow);
    RUN_TEST(TestKnifeAndAgentLoadouts);
    RUN_TEST(TestAgentRuntimeCoverage);
    RUN_TEST(TestKnifeRuntimeTable);
    RUN_TEST(TestProtocolValidationAndSnapshot);
    RUN_TEST(TestPanoramaContractVersion);
    RUN_TEST(TestPhase4MusicKitFlow);
    RUN_TEST(TestManualRevealQueue);
    RUN_TEST(TestMusicKitApplyRevision);
    RUN_TEST(TestNamedPipeTransport);
#undef RUN_TEST
    if (g_failures != 0) {
        std::printf("Inventory tests: %d error(es).\n", g_failures);
        return 1;
    }
    std::printf("Inventory tests: OK.\n");
    return 0;
}
