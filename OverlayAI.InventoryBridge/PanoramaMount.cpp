#include "PanoramaMount.h"

#include <windows.h>
#include <strsafe.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "BridgeLogging.h"
#include "InventorySocache.h"

namespace {
    using RunScriptFn = void(__fastcall*)(
        void*, void*, const char*, const char*, std::uint64_t);
    using MakeSymbolFn = std::int16_t(__fastcall*)(void*, int, const char*);
    using GetAttributeStringFn = const char* (__fastcall*)(
        void*, std::int16_t, const char*);
    using SetAttributeStringFn = void(__fastcall*)(
        void*, std::int16_t, const char*);

    enum class PendingAction : LONG {
        None = 0,
        Create = 1,
        Destroy = 2,
        RenderReveal = 3,
        RenderCollection = 4,
        InstallNativeLoadoutObserver = 5
    };

    struct PatternResult {
        DWORD count = 0;
        uintptr_t first = 0;
    };

    void* g_uiEngine = nullptr;
    uintptr_t g_mainMenuGlobal = 0;
    RunScriptFn g_runScript = nullptr;
    MakeSymbolFn g_makeSymbol = nullptr;
    std::int32_t g_getAttributeStringOffset = 0;
    std::int32_t g_setAttributeStringOffset = 0;
    std::int16_t g_commandSymbol = -1;
    std::int16_t g_actionSymbol = -1;
    volatile LONG g_commandAccessReady = 0;
    volatile LONG g_pendingCommand = 0;
    volatile LONG64 g_pendingCommandLocalId = 0;
    ULONGLONG g_nextCommandPollAt = 0;
    char g_lastRevealDiagnostic[64]{};
    uintptr_t g_lastScriptRootPanel = 0;
    uintptr_t g_mountedRootPanel = 0;
    volatile LONG g_pendingAction = static_cast<LONG>(PendingAction::None);
    volatile LONG g_mounted = 0;
    volatile LONG g_initialized = 0;
    volatile LONG g_collectionVisible = 0;
    volatile LONG g_useDebugPanoramaUi = 0;
    std::uint64_t g_revealIdentityWaitLocalId = 0;
    ULONGLONG g_revealIdentityWaitStartedAt = 0;
    ULONGLONG g_nextRevealIdentityRetryAt = 0;
    SRWLOCK g_revealLock = SRWLOCK_INIT;

    struct RevealSnapshot {
        bool available = false;
        bool equipped = false;
        std::uint64_t localId = 0;
        std::uint32_t pendingCount = 0;
        std::uint64_t hash = 0;
        char itemJson[4096]{};
        char itemsJson[65536]{};
    };

    RevealSnapshot g_reveal{};
    RevealSnapshot g_collection{};

    constexpr char kCreateScript[] = R"JS(
(function () {
    var root = $.GetContextPanel();
    var previous = root.FindChildTraverse('OverlayAIPanoramaProbe');
    if (previous) previous.DeleteAsync(0.0);
    var panel = $.CreatePanel('Panel', root, 'OverlayAIPanoramaProbe');
    panel.style.width = '360px';
    panel.style.height = '64px';
    panel.style.horizontalAlign = 'center';
    panel.style.verticalAlign = 'top';
    panel.style.marginTop = '92px';
    panel.style.backgroundColor = 'rgba(8, 18, 28, 0.92)';
    panel.style.border = '1px solid rgba(65, 183, 255, 0.90)';
    panel.style.flowChildren = 'right';
    var label = $.CreatePanel('Label', panel, 'OverlayAIPanoramaProbeLabel');
    label.text = 'OverlayAI Panorama bridge';
    label.style.horizontalAlign = 'center';
    label.style.verticalAlign = 'center';
    label.style.color = 'white';
    label.style.fontSize = '22px';
})())JS";

    constexpr char kNativeLoadoutObserverScript[] = R"JS(
(function () {
    var root = $.GetContextPanel();
    var data = root.Data();
    if (data.overlayAINativeEquipWrapper) return;
    if (typeof LoadoutAPI === 'undefined' ||
        typeof LoadoutAPI.EquipItemInSlot !== 'function') {
        root.SetAttributeString('overlayai_cmd',
            'diag:native-equip-api-missing');
        return;
    }

    var original = LoadoutAPI.EquipItemInSlot;
    var wrapper = function () {
        var values = [];
        for (var index = 0; index < arguments.length; ++index)
            values.push(String(arguments[index]));
        root.SetAttributeString('overlayai_action',
            'nativeequip:' + values.join(','));
        return original.apply(LoadoutAPI, arguments);
    };

    try {
        LoadoutAPI.EquipItemInSlot = wrapper;
    } catch (error) {
        root.SetAttributeString('overlayai_cmd',
            'diag:native-equip-wrapper-exception');
        return;
    }
    if (LoadoutAPI.EquipItemInSlot !== wrapper) {
        root.SetAttributeString('overlayai_cmd',
            'diag:native-equip-wrapper-rejected');
        return;
    }
    data.overlayAIOriginalEquipItemInSlot = original;
    data.overlayAINativeEquipWrapper = wrapper;
    root.SetAttributeString('overlayai_cmd',
        'diag:native-equip-wrapper-ready');
})())JS";

    constexpr char kDestroyScript[] = R"JS(
(function () {
    var context = $.GetContextPanel();
    var data = context.Data();
    if (data.overlayAIRevealTabHandler) {
        $.UnregisterForUnhandledEvent(
            'MainMenuTabShown', data.overlayAIRevealTabHandler);
        data.overlayAIRevealTabHandler = 0;
    }
    if (data.overlayAIRevealMenuHandler) {
        $.UnregisterForUnhandledEvent(
            'CSGOShowMainMenu', data.overlayAIRevealMenuHandler);
        data.overlayAIRevealMenuHandler = 0;
    }
    if (data.overlayAIRevealInventoryHandler) {
        $.UnregisterForUnhandledEvent(
            'PanoramaComponent_MyPersona_InventoryUpdated',
            data.overlayAIRevealInventoryHandler);
        data.overlayAIRevealInventoryHandler = 0;
    }
    if (data.overlayAIRevealPopupHandler) {
        $.UnregisterForUnhandledEvent(
            'UIPopupButtonClicked', data.overlayAIRevealPopupHandler);
        data.overlayAIRevealPopupHandler = 0;
    }
    if (data.overlayAIOriginalEquipItemInSlot &&
        typeof LoadoutAPI !== 'undefined') {
        try {
            LoadoutAPI.EquipItemInSlot =
                data.overlayAIOriginalEquipItemInSlot;
        } catch (error) {}
        data.overlayAIOriginalEquipItemInSlot = null;
        data.overlayAINativeEquipWrapper = null;
    }
    var probe = context.FindChildTraverse('OverlayAIPanoramaProbe');
    var reveal = context.FindChildTraverse('OverlayAILocalInventoryRoot');
    var pendingBadge = context.FindChildTraverse('OverlayAIInventoryPendingBadge');
    if (probe) probe.DeleteAsync(0.0);
    if (reveal) reveal.DeleteAsync(0.0);
    if (pendingBadge) pendingBadge.DeleteAsync(0.0);
    context.SetAttributeString('overlayai_native_reveal_id', '');
    context.SetAttributeString('overlayai_native_reveal_state', 'idle');

    var alert = context.FindChildTraverse('MainMenuInvAlert');
    if (alert && typeof InventoryAPI !== 'undefined') {
        var count = Number(InventoryAPI.GetUnacknowledgeItemsCount() || 0);
        alert.SetDialogVariable('alert_value', String(count));
        alert.SetHasClass('hidden', count < 1);
    }
})())JS";

    constexpr char kQueuedRevealScriptTemplate[] = R"JS(
(function () {
    var root = $.GetContextPanel();
    var items = __ITEMS__;
    var expectedCount = __COUNT__;
    var firstLocalId = '__LOCAL_ID__';
    var firstItemId = '__ITEM_ID__';
    var data = root.Data();

    function refreshBadge() {
        if (!root || !root.IsValid()) return;
        var inventoryButton = root.FindChildTraverse('MainMenuNavBarInventory');
        if (!inventoryButton) return;
        var badge = inventoryButton.FindChildTraverse(
            'OverlayAIInventoryPendingBadge');
        if (expectedCount < 1) {
            if (badge) badge.DeleteAsync(0.0);
            return;
        }
        if (!badge) {
            badge = $.CreatePanel('Label', inventoryButton,
                'OverlayAIInventoryPendingBadge');
            badge.style.width = '18px';
            badge.style.height = '18px';
            badge.style.horizontalAlign = 'center';
            badge.style.verticalAlign = 'top';
            badge.style.marginTop = '34px';
            badge.style.backgroundColor = '#A75010';
            badge.style.color = 'white';
            badge.style.fontSize = '13px';
            badge.style.fontWeight = 'bold';
            badge.style.textAlign = 'center';
            badge.style.zIndex = '50';
        }
        badge.text = String(expectedCount);
        root.SetAttributeString('overlayai_native_reveal_count',
            String(expectedCount));
    }

    function closeQueue(openLoadout) {
        var panel = root.FindChildTraverse('OverlayAILocalInventoryRoot');
        if (panel) panel.DeleteAsync(0.0);
        function publishClose() {
            if (root)
                root.SetAttributeString('overlayai_action',
                    'close:' + firstLocalId);
        }
        publishClose();
        // Inventory events can replace the shared diagnostic attribute in the
        // same frame. One delayed retry keeps the action reliable without a
        // permanent Panorama poll.
        $.Schedule(0.12, publishClose);
        if (openLoadout) {
            $.Schedule(0.05, function () {
                var loadout = root.FindChildTraverse('MainMenuNavBarLoadout');
                if (loadout) $.DispatchEvent('Activated', loadout, 'mouse');
            });
        }
    }

    function showPendingItems(tab) {
        if (String(tab) !== 'JsInventory' || !items.length) return;
        $.Schedule(0.08, function () {
            if (!root || !root.IsValid() ||
                root.FindChildTraverse('OverlayAILocalInventoryRoot')) return;

            var rootWidth = Number(root.actuallayoutwidth || 1920);
            var rootHeight = Number(root.actuallayoutheight || 1080);
            var scale = Math.max(0.68, Math.min(1.15,
                Math.min(rootWidth / 1920, rootHeight / 1080)));
            function px(value) { return Math.round(value * scale) + 'px'; }
            function colorFor(item) {
                var value = Number(item.rarity_color || 4942335).toString(16);
                while (value.length < 6) value = '0' + value;
                return '#' + value.slice(-6);
            }
            function addButton(parent, id, text, width) {
                var button = $.CreatePanel('Button', parent, id);
                button.style.width = px(width);
                button.style.height = px(48);
                button.style.marginLeft = px(7);
                button.style.marginRight = px(7);
                button.style.backgroundColor = '#24364A';
                button.style.border = '1px solid #4B6E91';
                var label = $.CreatePanel('Label', button, id + 'Label');
                label.text = text;
                label.style.horizontalAlign = 'center';
                label.style.verticalAlign = 'center';
                label.style.color = 'white';
                label.style.fontSize = px(16);
                label.style.fontWeight = 'semibold';
                return button;
            }

            var backdrop = $.CreatePanel('Panel', root,
                'OverlayAILocalInventoryRoot');
            backdrop.style.width = '100%';
            backdrop.style.height = '100%';
            backdrop.style.horizontalAlign = 'center';
            backdrop.style.verticalAlign = 'center';
            backdrop.style.backgroundColor = 'rgba(0, 0, 0, 0.52)';

            var card = $.CreatePanel('Panel', backdrop, 'OverlayAIRevealCard');
            card.style.width = px(1080);
            card.style.height = px(575);
            card.style.horizontalAlign = 'center';
            card.style.verticalAlign = 'center';
            card.style.backgroundColor = 'rgba(8, 15, 24, 0.985)';
            card.style.flowChildren = 'down';
            card.style.opacity = '0.0';

            var header = $.CreatePanel('Label', card, 'OverlayAIRevealHeader');
            header.text = 'NUEVO ARTICULO';
            header.style.marginLeft = px(72);
            header.style.marginTop = px(34);
            header.style.color = 'white';
            header.style.fontSize = px(38);
            header.style.fontWeight = 'bold';

            var content = $.CreatePanel('Panel', card, 'OverlayAIRevealContent');
            content.style.width = '100%';
            content.style.height = px(390);

            var image = $.CreatePanel('Image', content, 'OverlayAIRevealImage');
            image.style.width = px(330);
            image.style.height = px(330);
            image.style.horizontalAlign = 'center';
            image.style.verticalAlign = 'center';

            var previous = $.CreatePanel('Button', content, 'OverlayAIRevealPrev');
            previous.style.width = px(70);
            previous.style.height = px(110);
            previous.style.verticalAlign = 'center';
            previous.style.marginLeft = px(18);
            var previousLabel = $.CreatePanel('Label', previous,
                'OverlayAIRevealPrevLabel');
            previousLabel.text = '<';
            previousLabel.style.horizontalAlign = 'center';
            previousLabel.style.verticalAlign = 'center';
            previousLabel.style.fontSize = px(64);
            previousLabel.style.color = 'white';

            var next = $.CreatePanel('Button', content, 'OverlayAIRevealNext');
            next.style.width = px(70);
            next.style.height = px(110);
            next.style.horizontalAlign = 'right';
            next.style.verticalAlign = 'center';
            next.style.marginRight = px(18);
            var nextLabel = $.CreatePanel('Label', next, 'OverlayAIRevealNextLabel');
            nextLabel.text = '>';
            nextLabel.style.horizontalAlign = 'center';
            nextLabel.style.verticalAlign = 'center';
            nextLabel.style.fontSize = px(64);
            nextLabel.style.color = 'white';

            var name = $.CreatePanel('Label', card, 'OverlayAIRevealName');
            name.style.marginLeft = px(72);
            name.style.marginTop = px(-58);
            name.style.color = 'white';
            name.style.fontSize = px(25);
            name.style.fontWeight = 'semibold';

            var counter = $.CreatePanel('Label', card, 'OverlayAIRevealCounter');
            counter.style.horizontalAlign = 'right';
            counter.style.marginRight = px(76);
            counter.style.marginTop = px(-34);
            counter.style.color = '#D5DBE5';
            counter.style.fontSize = px(17);

            var actions = $.CreatePanel('Panel', card, 'OverlayAIRevealActions');
            actions.style.width = px(650);
            actions.style.height = px(54);
            actions.style.horizontalAlign = 'right';
            actions.style.marginRight = px(54);
            actions.style.marginTop = px(14);
            actions.style.flowChildren = 'right';
            var loadoutButton = addButton(actions, 'OverlayAIRevealLoadout',
                'VER EN LOS ARTICULOS EQUIPADOS', 390);
            var continueButton = addButton(actions, 'OverlayAIRevealContinue',
                'CONTINUAR', 230);

            var index = 0;
            function render() {
                var item = items[index];
                var rarity = colorFor(item);
                card.style.border = '2px solid ' + rarity;
                if (item.image_url) image.SetImage(String(item.image_url));
                name.text = String(item.custom_name || item.display_name ||
                    'Articulo local');
                counter.text = String(index + 1) + ' / ' + String(items.length);
                previous.visible = items.length > 1;
                next.visible = items.length > 1;
            }
            previous.SetPanelEvent('onactivate', function () {
                index = (index + items.length - 1) % items.length;
                render();
            });
            next.SetPanelEvent('onactivate', function () {
                index = (index + 1) % items.length;
                render();
            });
            loadoutButton.SetPanelEvent('onactivate', function () {
                closeQueue(true);
            });
            continueButton.SetPanelEvent('onactivate', function () {
                closeQueue(false);
            });
            render();
            card.style.opacity = '1.0';
            root.SetAttributeString('overlayai_native_reveal_state',
                'inventory_opened');
        });
    }

    var oldPanel = root.FindChildTraverse('OverlayAILocalInventoryRoot');
    if (oldPanel) oldPanel.DeleteAsync(0.0);
    if (data.overlayAIRevealTabHandler) {
        $.UnregisterForUnhandledEvent(
            'MainMenuTabShown', data.overlayAIRevealTabHandler);
    }
    data.overlayAIRevealTabHandler = $.RegisterForUnhandledEvent(
        'MainMenuTabShown', showPendingItems);
    if (!data.overlayAIRevealMenuHandler) {
        data.overlayAIRevealMenuHandler = $.RegisterForUnhandledEvent(
            'CSGOShowMainMenu', refreshBadge);
    }
    if (!data.overlayAIRevealInventoryHandler) {
        data.overlayAIRevealInventoryHandler = $.RegisterForUnhandledEvent(
            'PanoramaComponent_MyPersona_InventoryUpdated', refreshBadge);
    }

    root.SetAttributeString('overlayai_native_reveal_id', firstLocalId);
    root.SetAttributeString('overlayai_native_reveal_expected_item_id', firstItemId);
    root.SetAttributeString('overlayai_native_reveal_expected_count',
        String(expectedCount));
    root.SetAttributeString('overlayai_native_reveal_state', 'waiting_inventory');
    refreshBadge();
    $.Schedule(0.10, refreshBadge);
})())JS";

    constexpr char kNativeRevealObserverScript[] = R"JS(
(function () {
    var root = $.GetContextPanel();
    var data = root.Data();
    var pendingLocalId = '__LOCAL_ID__';

    if (data.overlayAIRevealPopupHandler) {
        $.UnregisterForUnhandledEvent(
            'UIPopupButtonClicked', data.overlayAIRevealPopupHandler);
        data.overlayAIRevealPopupHandler = 0;
    }
    if (pendingLocalId && pendingLocalId !== '0') {
        data.overlayAIRevealPopupHandler = $.RegisterForUnhandledEvent(
            'UIPopupButtonClicked', function () {
                // CS2 cannot persist acknowledgement of a local-only item in
                // Steam. Mirror the native popup confirmation to the Overlay.
                root.SetAttributeString('overlayai_action',
                    'close:' + pendingLocalId);
            });
    }

    function syncNativeAlert() {
        if (!root || !root.IsValid()) return;
        var badge = root.FindChildTraverse('OverlayAIInventoryPendingBadge');
        if (badge) badge.DeleteAsync(0.0);
        var customRoot = root.FindChildTraverse('OverlayAILocalInventoryRoot');
        if (customRoot) customRoot.DeleteAsync(0.0);
        var alert = root.FindChildTraverse('MainMenuInvAlert');
        if (alert && typeof InventoryAPI !== 'undefined') {
            var count = Number(InventoryAPI.GetUnacknowledgeItemsCount() || 0);
            alert.SetDialogVariable('alert_value', String(count));
            alert.SetHasClass('hidden', count < 1);
            if (data.overlayAINativeUnacknowledgedCount !== count) {
                data.overlayAINativeUnacknowledgedCount = count;
                root.SetAttributeString('overlayai_cmd',
                    'diag:native-unacknowledged-count-' + String(count));
            }
        }
    }

    if (data.overlayAIRevealTabHandler) {
        $.UnregisterForUnhandledEvent(
            'MainMenuTabShown', data.overlayAIRevealTabHandler);
        data.overlayAIRevealTabHandler = 0;
    }
    data.overlayAIRevealTabHandler = $.RegisterForUnhandledEvent(
        'MainMenuTabShown', function (tab) {
            if (String(tab) !== 'JsInventory') return;
            var count = typeof InventoryAPI !== 'undefined'
                ? Number(InventoryAPI.GetUnacknowledgeItemsCount() || 0) : -1;
            root.SetAttributeString('overlayai_cmd',
                'diag:native-inventory-opened-count-' + String(count));
            $.Schedule(0.10, syncNativeAlert);
        });
    if (!data.overlayAIRevealMenuHandler) {
        data.overlayAIRevealMenuHandler = $.RegisterForUnhandledEvent(
            'CSGOShowMainMenu', syncNativeAlert);
    }
    if (!data.overlayAIRevealInventoryHandler) {
        data.overlayAIRevealInventoryHandler = $.RegisterForUnhandledEvent(
            'PanoramaComponent_MyPersona_InventoryUpdated', syncNativeAlert);
    }

    root.SetAttributeString('overlayai_native_reveal_state', 'native_observer');
    syncNativeAlert();
    // Wake Panorama once after a completed SOCache batch. Without this event,
    // knife reveals may wait for an unrelated inventory mutation.
    $.DispatchEvent('PanoramaComponent_MyPersona_InventoryUpdated');
    $.Schedule(0.10, syncNativeAlert);
})())JS";

    constexpr char kRevealScriptFormat[] = R"JS(
(function () {
    var root = $.GetContextPanel();
    var item = %s;
    var equipped = %s;
    var typeNames = ['MUSIC KIT', 'ARMA', 'CUCHILLO', 'GUANTES', 'AGENTE',
        'COLECCIONABLE', 'CAJA / CONTENEDOR', 'LLAVE', 'STICKER'];
    var typeName = typeNames[Number(item.type)] || 'ARTICULO';
    var rarity = Number(item.rarity_color || 4942335).toString(16);
    while (rarity.length < 6) rarity = '0' + rarity;
    rarity = '#' + rarity.slice(-6);

    var card = root.FindChildTraverse('OverlayAILocalInventoryRoot');
    if (!card) {
        card = $.CreatePanel('Panel', root, 'OverlayAILocalInventoryRoot');
    } else {
        card.RemoveAndDeleteChildren();
    }
    var rootWidth = Number(root.actuallayoutwidth || 1920);
    var rootHeight = Number(root.actuallayoutheight || 1080);
    var scale = Math.max(0.68, Math.min(1.15,
        Math.min(rootWidth / 1920, rootHeight / 1080)));
    function px(value) { return Math.round(value * scale) + 'px'; }
    card.SetAttributeString('overlayai_view', 'reveal');
    card.SetAttributeString('overlayai_local_id', String(item.local_id));
    card.style.width = px(760);
    card.style.height = px(590);
    card.style.horizontalAlign = 'center';
    card.style.verticalAlign = 'center';
    card.style.backgroundColor = 'rgba(8, 15, 24, 0.97)';
    card.style.border = '2px solid ' + rarity;
    card.style.flowChildren = 'down';

    var header = $.CreatePanel('Label', card, 'OverlayAIRevealHeader');
    header.text = 'NEW ITEM';
    header.style.horizontalAlign = 'center';
    header.style.marginTop = px(24);
    header.style.color = 'white';
    header.style.fontSize = px(34);
    header.style.fontWeight = 'bold';
    header.style.letterSpacing = '3px';

    var type = $.CreatePanel('Label', card, 'OverlayAIRevealType');
    type.text = typeName;
    type.style.horizontalAlign = 'center';
    type.style.marginTop = px(4);
    type.style.color = rarity;
    type.style.fontSize = px(18);
    type.style.letterSpacing = '2px';

    var image = $.CreatePanel('Image', card, 'OverlayAIRevealImage');
    image.style.width = px(250);
    image.style.height = px(250);
    image.style.horizontalAlign = 'center';
    image.style.marginTop = px(18);
    if (item.image_url) image.SetImage(String(item.image_url));

    var name = $.CreatePanel('Label', card, 'OverlayAIRevealName');
    name.text = String(item.custom_name || item.display_name || 'Articulo local');
    name.style.horizontalAlign = 'center';
    name.style.marginTop = px(12);
    name.style.color = 'white';
    name.style.fontSize = px(25);
    name.style.fontWeight = 'semibold';

    var rarityLabel = $.CreatePanel('Label', card, 'OverlayAIRevealRarity');
    rarityLabel.text = String(item.rarity || 'High Grade').toUpperCase();
    rarityLabel.style.horizontalAlign = 'center';
    rarityLabel.style.marginTop = px(4);
    rarityLabel.style.color = rarity;
    rarityLabel.style.fontSize = px(17);

    var status = $.CreatePanel('Label', card, 'OverlayAIRevealStatus');
    status.text = equipped ? 'EQUIPADO LOCALMENTE' : 'LISTO EN TU INVENTARIO LOCAL';
    status.style.horizontalAlign = 'center';
    status.style.marginTop = px(13);
    status.style.color = equipped ? '#7EE787' : '#A8B3C2';
    status.style.fontSize = px(16);

    var actions = $.CreatePanel('Panel', card, 'OverlayAIRevealActions');
    actions.style.width = px(520);
    actions.style.height = px(54);
    actions.style.horizontalAlign = 'center';
    actions.style.marginTop = px(18);
    actions.style.flowChildren = 'right';

    function makeButton(id, text) {
        var button = $.CreatePanel('Button', actions, id);
        button.style.width = px(250);
        button.style.height = px(48);
        button.style.marginLeft = px(7);
        button.style.marginRight = px(7);
        button.style.backgroundColor = '#24364A';
        button.style.border = '1px solid #4B6E91';
        var label = $.CreatePanel('Label', button, id + 'Label');
        label.text = text;
        label.style.horizontalAlign = 'center';
        label.style.verticalAlign = 'center';
        label.style.color = 'white';
        label.style.fontSize = px(17);
        label.style.fontWeight = 'semibold';
        return button;
    }

    var equipButton = makeButton('OverlayAIRevealEquip',
        equipped ? 'EQUIPADO' : 'EQUIPAR');
    equipButton.enabled = !equipped;
    if (equipped) equipButton.style.opacity = '0.45';
    equipButton.SetPanelEvent('onactivate', function () {
        if (equipped) return;
        root.SetAttributeString('overlayai_action', 'equip:' + String(item.local_id));
        status.text = 'APLICANDO AL LOADOUT...';
        equipButton.enabled = false;
        equipButton.style.opacity = '0.45';
    });

    var continueButton = makeButton('OverlayAIRevealContinue', 'CONTINUAR');
    continueButton.SetPanelEvent('onactivate', function () {
        function publishClose() {
            if (root)
                root.SetAttributeString('overlayai_action',
                    'close:' + String(item.local_id));
        }
        publishClose();
        $.Schedule(0.12, publishClose);
        status.text = 'GUARDANDO EN TU COLECCION...';
        continueButton.enabled = false;
        continueButton.style.opacity = '0.45';
    });
})())JS";

    constexpr char kCollectionScriptFormat[] = R"JS(
(function () {
    var root = $.GetContextPanel();
    var item = %s;
    var equipped = %s;
    var typeNames = ['MUSIC KIT', 'ARMA', 'CUCHILLO', 'GUANTES', 'AGENTE',
        'COLECCIONABLE', 'CAJA / CONTENEDOR', 'LLAVE', 'STICKER'];
    var typeName = typeNames[Number(item.type)] || 'ARTICULO';
    var rarity = Number(item.rarity_color || 4942335).toString(16);
    while (rarity.length < 6) rarity = '0' + rarity;
    rarity = '#' + rarity.slice(-6);

    var card = root.FindChildTraverse('OverlayAILocalInventoryRoot');
    if (!card) {
        card = $.CreatePanel('Panel', root, 'OverlayAILocalInventoryRoot');
    } else {
        card.RemoveAndDeleteChildren();
    }
    var rootWidth = Number(root.actuallayoutwidth || 1920);
    var rootHeight = Number(root.actuallayoutheight || 1080);
    var scale = Math.max(0.68, Math.min(1.15,
        Math.min(rootWidth / 1920, rootHeight / 1080)));
    function px(value) { return Math.round(value * scale) + 'px'; }
    card.SetAttributeString('overlayai_view', 'collection');
    card.SetAttributeString('overlayai_local_id', String(item.local_id));
    card.style.width = px(860);
    card.style.height = px(500);
    card.style.horizontalAlign = 'center';
    card.style.verticalAlign = 'center';
    card.style.backgroundColor = 'rgba(8, 15, 24, 0.97)';
    card.style.border = '2px solid ' + rarity;
    card.style.flowChildren = 'down';

    var header = $.CreatePanel('Label', card, 'OverlayAICollectionHeader');
    header.text = 'TU COLECCION';
    header.style.horizontalAlign = 'center';
    header.style.marginTop = px(26);
    header.style.color = 'white';
    header.style.fontSize = px(31);
    header.style.fontWeight = 'bold';
    header.style.letterSpacing = '3px';

    var subtitle = $.CreatePanel('Label', card, 'OverlayAICollectionSubtitle');
    subtitle.text = 'ARTICULO ANADIDO CORRECTAMENTE';
    subtitle.style.horizontalAlign = 'center';
    subtitle.style.marginTop = px(5);
    subtitle.style.color = '#A8B3C2';
    subtitle.style.fontSize = px(15);

    var content = $.CreatePanel('Panel', card, 'OverlayAICollectionContent');
    content.style.width = '88%%';
    content.style.height = px(300);
    content.style.horizontalAlign = 'center';
    content.style.marginTop = px(22);
    content.style.flowChildren = 'right';

    var image = $.CreatePanel('Image', content, 'OverlayAICollectionImage');
    image.style.width = px(260);
    image.style.height = px(260);
    image.style.verticalAlign = 'center';
    if (item.image_url) image.SetImage(String(item.image_url));

    var details = $.CreatePanel('Panel', content, 'OverlayAICollectionDetails');
    details.style.width = 'fill-parent-flow(1.0)';
    details.style.height = '100%%';
    details.style.marginLeft = px(34);
    details.style.flowChildren = 'down';

    function addLabel(id, text, color, size, margin) {
        var label = $.CreatePanel('Label', details, id);
        label.text = text;
        label.style.marginTop = px(margin);
        label.style.color = color;
        label.style.fontSize = px(size);
        return label;
    }
    addLabel('OverlayAICollectionType', typeName, rarity, 17, 30);
    var name = addLabel('OverlayAICollectionName',
        String(item.custom_name || item.display_name || 'Articulo local'),
        'white', 25, 10);
    name.style.fontWeight = 'semibold';
    addLabel('OverlayAICollectionRarity',
        String(item.rarity || 'High Grade').toUpperCase(), rarity, 17, 12);
    addLabel('OverlayAICollectionState',
        equipped ? 'EQUIPADO LOCALMENTE' : 'DISPONIBLE EN EL LOADOUT LOCAL',
        equipped ? '#7EE787' : '#A8B3C2', 16, 28);

    var close = $.CreatePanel('Button', card, 'OverlayAICollectionClose');
    close.style.width = px(250);
    close.style.height = px(48);
    close.style.horizontalAlign = 'center';
    close.style.marginTop = px(8);
    close.style.backgroundColor = '#24364A';
    close.style.border = '1px solid #4B6E91';
    var closeLabel = $.CreatePanel('Label', close, 'OverlayAICollectionCloseLabel');
    closeLabel.text = 'CERRAR';
    closeLabel.style.horizontalAlign = 'center';
    closeLabel.style.verticalAlign = 'center';
    closeLabel.style.color = 'white';
    closeLabel.style.fontSize = px(17);
    closeLabel.style.fontWeight = 'semibold';
    close.SetPanelEvent('onactivate', function () {
        root.SetAttributeString('overlayai_action',
            'dismiss:' + String(item.local_id));
        close.enabled = false;
        close.style.opacity = '0.45';
    });
})())JS";

    SIZE_T ImageSize(HMODULE module) noexcept {
        if (!module) return 0;
        const auto* base = reinterpret_cast<const unsigned char*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE
            ? nt->OptionalHeader.SizeOfImage : 0;
    }

    bool IsAddressInModule(uintptr_t address, HMODULE module) noexcept {
        const uintptr_t base = reinterpret_cast<uintptr_t>(module);
        const SIZE_T size = ImageSize(module);
        return address >= base && address < base + size;
    }

    inline bool IsValidUserPointer(const void* address, SIZE_T size = 1) noexcept {
        const uintptr_t ptr = reinterpret_cast<uintptr_t>(address);
        return ptr >= 0x10000 && ptr <= (0x7FFFFFFEFFFFull - size);
    }

    bool ReadPointer(uintptr_t address, uintptr_t& value) noexcept {
        value = 0;
        if (!IsValidUserPointer(reinterpret_cast<const void*>(address), sizeof(value)))
            return false;
        __try {
            CopyMemory(&value, reinterpret_cast<const void*>(address), sizeof(value));
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            value = 0;
            return false;
        }
    }

    bool HasExecutableFirstMethod(uintptr_t object, HMODULE vtableModule) noexcept {
        uintptr_t vtable = 0;
        uintptr_t firstMethod = 0;
        if (!ReadPointer(object, vtable) ||
            !IsAddressInModule(vtable, vtableModule) ||
            !ReadPointer(vtable, firstMethod))
            return false;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(firstMethod),
            &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (info.Protect & executable) != 0;
    }

    bool IsExecutableAddress(uintptr_t address) noexcept {
        MEMORY_BASIC_INFORMATION info{};
        if (!address || !VirtualQuery(reinterpret_cast<const void*>(address),
            &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (info.Protect & executable) != 0;
    }

    PatternResult ScanExecutableSections(
        HMODULE module, const unsigned char* bytes, const char* mask) noexcept {
        PatternResult result;
        if (!module || !bytes || !mask) return result;
        const SIZE_T patternLength = static_cast<SIZE_T>(lstrlenA(mask));
        if (!patternLength) return result;
        const auto* base = reinterpret_cast<const unsigned char*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return result;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return result;

        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections;
            ++index, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;
            const SIZE_T sectionSize = section->Misc.VirtualSize;
            if (sectionSize < patternLength) continue;
            const unsigned char* start = base + section->VirtualAddress;
            for (SIZE_T offset = 0; offset <= sectionSize - patternLength;
                ++offset) {
                bool matches = true;
                for (SIZE_T byte = 0; byte < patternLength; ++byte) {
                    if (mask[byte] == 'x' &&
                        start[offset + byte] != bytes[byte]) {
                        matches = false;
                        break;
                    }
                }
                if (!matches) continue;
                ++result.count;
                if (!result.first)
                    result.first = reinterpret_cast<uintptr_t>(start + offset);
            }
        }
        return result;
    }

    uintptr_t ResolveRelative(uintptr_t instruction, SIZE_T offset) noexcept {
        std::int32_t displacement = 0;
        const uintptr_t address = instruction + offset;
        if (!IsValidUserPointer(reinterpret_cast<const void*>(address), sizeof(displacement)))
            return 0;
        __try {
            CopyMemory(&displacement, reinterpret_cast<const void*>(address), sizeof(displacement));
            return address + sizeof(displacement) + displacement;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    bool ReadInteger32(uintptr_t address, std::int32_t& value) noexcept {
        value = 0;
        if (!IsValidUserPointer(reinterpret_cast<const void*>(address), sizeof(value)))
            return false;
        __try {
            CopyMemory(&value, reinterpret_cast<const void*>(address), sizeof(value));
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            value = 0;
            return false;
        }
    }

    const char* FindJsonValue(const char* json, const char* key) noexcept {
        if (!json || !key) return nullptr;
        char token[96]{};
        if (FAILED(StringCchPrintfA(token, _countof(token), "\"%s\":", key)))
            return nullptr;
        const char* found = strstr(json, token);
        return found ? found + lstrlenA(token) : nullptr;
    }

    bool ReadJsonUnsigned(
        const char* json, const char* key, std::uint64_t& value,
        const char* limit = nullptr) noexcept {
        const char* cursor = FindJsonValue(json, key);
        if (!cursor || (limit && cursor >= limit)) return false;
        std::uint64_t parsed = 0;
        bool hasDigit = false;
        while ((!limit || cursor < limit) && *cursor >= '0' && *cursor <= '9') {
            hasDigit = true;
            parsed = parsed * 10 + static_cast<std::uint64_t>(*cursor - '0');
            ++cursor;
        }
        if (!hasDigit) return false;
        value = parsed;
        return true;
    }

    bool ReadJsonBool(
        const char* json, const char* key, bool& value) noexcept {
        const char* cursor = FindJsonValue(json, key);
        if (!cursor) return false;
        if (strncmp(cursor, "true", 4) == 0) {
            value = true;
            return true;
        }
        if (strncmp(cursor, "false", 5) == 0) {
            value = false;
            return true;
        }
        return false;
    }

    bool FindItemById(
        const char* json, std::uint64_t localId,
        RevealSnapshot& reveal) noexcept {
        if (!json || localId == 0) return false;
        constexpr char localIdToken[] = "\"local_id\":";
        const char* cursor = json;
        while ((cursor = strstr(cursor, localIdToken)) != nullptr) {
            const char* value = cursor + sizeof(localIdToken) - 1;
            std::uint64_t parsedId = 0;
            bool hasDigit = false;
            while (*value >= '0' && *value <= '9') {
                hasDigit = true;
                parsedId = parsedId * 10 +
                    static_cast<std::uint64_t>(*value - '0');
                ++value;
            }
            if (!hasDigit || parsedId != localId) {
                cursor = value;
                continue;
            }

            const char* objectStart = cursor;
            while (objectStart > json && *objectStart != '{') --objectStart;
            const char* objectEnd = strchr(value, '}');
            std::uint64_t type = ~std::uint64_t{};
            if (*objectStart != '{' || !objectEnd ||
                !ReadJsonUnsigned(objectStart, "type", type, objectEnd) ||
                type >= 5)
                return false;

            const SIZE_T length = static_cast<SIZE_T>(
                objectEnd - objectStart + 1);
            if (length >= _countof(reveal.itemJson)) return false;
            CopyMemory(reveal.itemJson, objectStart, length);
            reveal.itemJson[length] = '\0';
            reveal.available = true;
            reveal.localId = localId;
            std::uint64_t equippedTeam = 0;
            (void)ReadJsonUnsigned(
                objectStart, "equipped_team", equippedTeam, objectEnd);
            reveal.equipped = equippedTeam != 0;
            return true;
        }
        return false;
    }

    bool FindPendingItem(
        const char* json, RevealSnapshot& reveal) noexcept {
        if (!json) return false;
        const char* cursor = FindJsonValue(json, "pending_reveal_item_ids");
        if (!cursor || *cursor != '[') return false;

        reveal.itemsJson[0] = '[';
        reveal.itemsJson[1] = '\0';
        std::uint32_t appended = 0;
        ++cursor;
        while (*cursor && *cursor != ']' && appended < 256) {
            while (*cursor == ' ' || *cursor == ',' || *cursor == '\t' ||
                   *cursor == '\r' || *cursor == '\n')
                ++cursor;
            if (*cursor == ']') break;
            std::uint64_t localId = 0;
            bool hasDigit = false;
            while (*cursor >= '0' && *cursor <= '9') {
                hasDigit = true;
                localId = localId * 10 +
                    static_cast<std::uint64_t>(*cursor - '0');
                ++cursor;
            }
            if (!hasDigit || localId == 0) break;

            RevealSnapshot item{};
            if (!FindItemById(json, localId, item)) continue;
            const SIZE_T used = strlen(reveal.itemsJson);
            const SIZE_T itemLength = strlen(item.itemJson);
            const SIZE_T extra = itemLength + (appended ? 1 : 0) + 2;
            if (used + extra >= _countof(reveal.itemsJson)) break;
            if (appended) StringCchCatA(
                reveal.itemsJson, _countof(reveal.itemsJson), ",");
            StringCchCatA(reveal.itemsJson, _countof(reveal.itemsJson),
                item.itemJson);
            if (appended == 0) {
                reveal.available = true;
                reveal.localId = item.localId;
                reveal.equipped = item.equipped;
                StringCchCopyA(reveal.itemJson, _countof(reveal.itemJson),
                    item.itemJson);
            }
            ++appended;
        }
        if (!appended) return false;
        StringCchCatA(reveal.itemsJson, _countof(reveal.itemsJson), "]");
        reveal.pendingCount = appended;
        return true;
    }

    std::uint64_t HashReveal(const RevealSnapshot& reveal) noexcept {
        constexpr std::uint64_t offset = 14695981039346656037ull;
        constexpr std::uint64_t prime = 1099511628211ull;
        std::uint64_t hash = offset;
        const unsigned char* cursor = reinterpret_cast<const unsigned char*>(
            reveal.itemsJson);
        while (*cursor) {
            hash ^= *cursor++;
            hash *= prime;
        }
        hash ^= reveal.equipped ? 1u : 0u;
        hash *= prime;
        hash ^= reveal.pendingCount;
        hash *= prime;
        return hash;
    }

    void ReplaceScriptToken(
        std::string& script, const char* token, const char* value) {
        const std::string needle = token ? token : "";
        const std::string replacement = value ? value : "";
        if (needle.empty()) return;
        std::size_t position = 0;
        while ((position = script.find(needle, position)) != std::string::npos) {
            script.replace(position, needle.length(), replacement);
            position += replacement.length();
        }
    }

    void* ResolveRootPanel() noexcept {
        HMODULE client = GetModuleHandleW(L"client.dll");
        HMODULE panorama = GetModuleHandleW(L"panorama.dll");
        uintptr_t wrapper = 0;
        uintptr_t rootPanel = 0;
        if (!client || !panorama ||
            !ReadPointer(g_mainMenuGlobal, wrapper) ||
            !HasExecutableFirstMethod(wrapper, client) ||
            !ReadPointer(wrapper + 0x8, rootPanel) ||
            !HasExecutableFirstMethod(rootPanel, panorama))
            return nullptr;
        return reinterpret_cast<void*>(rootPanel);
    }

    bool ExecuteScript(const char* script) noexcept {
        void* rootPanel = ResolveRootPanel();
        if (!g_runScript || !g_uiEngine || !rootPanel) return false;
        __try {
            g_runScript(g_uiEngine, rootPanel, script, "", 1);
            g_lastScriptRootPanel = reinterpret_cast<uintptr_t>(rootPanel);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool ResolveCommandAccess(void* rootPanel) noexcept {
        if (InterlockedCompareExchange(&g_commandAccessReady, 0, 0) != 0)
            return true;
        if (!rootPanel || !g_makeSymbol ||
            g_getAttributeStringOffset <= 0 ||
            g_setAttributeStringOffset <= 0)
            return false;

        uintptr_t vtable = 0;
        uintptr_t getAttribute = 0;
        uintptr_t setAttribute = 0;
        if (!ReadPointer(reinterpret_cast<uintptr_t>(rootPanel), vtable) ||
            !ReadPointer(vtable + g_getAttributeStringOffset, getAttribute) ||
            !ReadPointer(vtable + g_setAttributeStringOffset, setAttribute) ||
            !IsExecutableAddress(getAttribute) ||
            !IsExecutableAddress(setAttribute))
            return false;

        std::int16_t symbol = -1;
        std::int16_t actionSymbol = -1;
        __try {
            symbol = g_makeSymbol(g_uiEngine, 0, "overlayai_cmd");
            actionSymbol = g_makeSymbol(
                g_uiEngine, 0, "overlayai_action");
            if (symbol >= 0 && actionSymbol >= 0) {
                reinterpret_cast<SetAttributeStringFn>(setAttribute)(
                    rootPanel, symbol, "");
                reinterpret_cast<SetAttributeStringFn>(setAttribute)(
                    rootPanel, actionSymbol, "");
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            symbol = -1;
            actionSymbol = -1;
        }
        if (symbol < 0 || actionSymbol < 0) return false;
        g_commandSymbol = symbol;
        g_actionSymbol = actionSymbol;
        InterlockedExchange(&g_commandAccessReady, 1);
        AppendLog("Panorama commands: atributo validado en game thread.");
        return true;
    }

    bool ParsePanelCommand(
        const char* text, PanoramaInventoryCommand& command) noexcept {
        command = {};
        if (!text || !*text) return false;
        const char* digits = nullptr;
        if (strncmp(text, "nativeequip:", 12) == 0) {
            command.type = PanoramaInventoryCommandType::NativeEquipItem;
            const char* cursor = text + 12;
            std::uint64_t itemId = 0;
            while (*cursor) {
                if (*cursor < '0' || *cursor > '9') {
                    ++cursor;
                    continue;
                }
                std::uint64_t value = 0;
                while (*cursor >= '0' && *cursor <= '9') {
                    value = value * 10 +
                        static_cast<std::uint64_t>(*cursor - '0');
                    ++cursor;
                }
                if (value >= 0x100000000ull) itemId = value;
            }
            if (itemId == 0) return false;
            command.localId = itemId;
            return true;
        } else if (strncmp(text, "equip:", 6) == 0) {
            command.type = PanoramaInventoryCommandType::Equip;
            digits = text + 6;
        } else if (strncmp(text, "close:", 6) == 0) {
            command.type = PanoramaInventoryCommandType::CloseReveal;
            digits = text + 6;
        } else if (strncmp(text, "dismiss:", 8) == 0) {
            command.type = PanoramaInventoryCommandType::DismissCollection;
            digits = text + 8;
        } else {
            return false;
        }

        std::uint64_t localId = 0;
        bool hasDigit = false;
        while (*digits >= '0' && *digits <= '9') {
            hasDigit = true;
            localId = localId * 10 +
                static_cast<std::uint64_t>(*digits - '0');
            ++digits;
        }
        if (!hasDigit || *digits != '\0' || localId == 0) return false;
        command.localId = localId;
        return true;
    }

    void PollPanoramaCommand() noexcept {
        if (InterlockedCompareExchange(&g_mounted, 0, 0) == 0)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now < g_nextCommandPollAt) return;
        g_nextCommandPollAt = now + 50;

        void* rootPanel = ResolveRootPanel();
        if (!rootPanel) return;
        if (g_mountedRootPanel != 0 &&
            reinterpret_cast<uintptr_t>(rootPanel) != g_mountedRootPanel) {
            InterlockedExchange(&g_mounted, 0);
            AcquireSRWLockShared(&g_revealLock);
            const bool collectionAvailable = g_collection.available &&
                InterlockedCompareExchange(&g_collectionVisible, 0, 0) != 0;
            const bool revealAvailable = g_reveal.available;
            ReleaseSRWLockShared(&g_revealLock);
            if (collectionAvailable)
                InterlockedExchange(&g_pendingAction,
                    static_cast<LONG>(PendingAction::RenderCollection));
            else if (revealAvailable)
                InterlockedExchange(&g_pendingAction,
                    static_cast<LONG>(PendingAction::RenderReveal));
            else
                InterlockedExchange(&g_pendingAction, static_cast<LONG>(
                    PendingAction::InstallNativeLoadoutObserver));
            AppendLog("Panorama mount: root reconstruido; render reencolado.");
            return;
        }
        if (!ResolveCommandAccess(rootPanel)) return;
        uintptr_t vtable = 0;
        uintptr_t getAttribute = 0;
        uintptr_t setAttribute = 0;
        if (!ReadPointer(reinterpret_cast<uintptr_t>(rootPanel), vtable) ||
            !ReadPointer(vtable + g_getAttributeStringOffset, getAttribute) ||
            !ReadPointer(vtable + g_setAttributeStringOffset, setAttribute) ||
            !IsExecutableAddress(getAttribute) ||
            !IsExecutableAddress(setAttribute))
            return;

        char text[64]{};
        __try {
            // Actions have a dedicated mailbox so inventory diagnostics cannot
            // overwrite a user click before this 50 ms poll observes it.
            const char* value = reinterpret_cast<GetAttributeStringFn>(
                getAttribute)(rootPanel, g_actionSymbol, "");
            if (value) StringCchCopyNA(text, _countof(text), value, 63);
            if (text[0]) reinterpret_cast<SetAttributeStringFn>(setAttribute)(
                rootPanel, g_actionSymbol, "");
            if (!text[0]) {
                value = reinterpret_cast<GetAttributeStringFn>(
                    getAttribute)(rootPanel, g_commandSymbol, "");
                if (value) StringCchCopyNA(text, _countof(text), value, 63);
            }
            if (text[0]) reinterpret_cast<SetAttributeStringFn>(setAttribute)(
                rootPanel, g_commandSymbol, "");
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            text[0] = '\0';
        }
        if (!text[0]) return;

        if (strncmp(text, "diag:", 5) == 0) {
            if (strncmp(text, g_lastRevealDiagnostic,
                    _countof(g_lastRevealDiagnostic) - 1) != 0) {
                StringCchCopyA(g_lastRevealDiagnostic,
                    _countof(g_lastRevealDiagnostic), text);
                char message[160]{};
                StringCchPrintfA(message, _countof(message),
                    "Panorama reveal diagnostic: %s.", text + 5);
                AppendLog(message);
            }
            return;
        }

        PanoramaInventoryCommand command{};
        if (!ParsePanelCommand(text, command)) {
            AppendLog("Panorama commands: comando invalido descartado.");
            return;
        }

        AcquireSRWLockShared(&g_revealLock);
        const bool matchesView = command.type ==
            PanoramaInventoryCommandType::NativeEquipItem
            ? true
            : command.type == PanoramaInventoryCommandType::DismissCollection
            ? g_collection.available && g_collection.localId == command.localId &&
                InterlockedCompareExchange(&g_collectionVisible, 0, 0) != 0
            : g_reveal.available && g_reveal.localId == command.localId;
        ReleaseSRWLockShared(&g_revealLock);
        if (!matchesView) {
            AppendLog("Panorama commands: Local ID obsoleto descartado.");
            return;
        }

        if (command.type == PanoramaInventoryCommandType::DismissCollection) {
            InterlockedExchange(&g_collectionVisible, 0);
            InterlockedExchange(&g_pendingAction,
                static_cast<LONG>(PendingAction::Destroy));
            AppendLog("Panorama commands: coleccion cerrada localmente.");
            return;
        }

        InterlockedExchange64(&g_pendingCommandLocalId,
            static_cast<LONG64>(command.localId));
        InterlockedExchange(&g_pendingCommand,
            static_cast<LONG>(command.type));
        AppendLog(command.type == PanoramaInventoryCommandType::Equip
            ? "Panorama commands: equip encolado."
            : command.type == PanoramaInventoryCommandType::NativeEquipItem
                ? "Panorama commands: intento equip nativo encolado."
                : "Panorama commands: close reveal encolado.");
    }
}

bool InitializePanoramaMount(void* panoramaInterface) noexcept {
    HMODULE client = GetModuleHandleW(L"client.dll");
    HMODULE panorama = GetModuleHandleW(L"panorama.dll");
    uintptr_t uiEngine = 0;
    if (!panoramaInterface || !client || !panorama ||
        !ReadPointer(reinterpret_cast<uintptr_t>(panoramaInterface) + 0x28,
            uiEngine) || !HasExecutableFirstMethod(uiEngine, panorama)) {
        AppendLog("Panorama mount: CUIEngine no valido.");
        return false;
    }

    constexpr unsigned char mainMenuPattern[] = {
        0x48, 0x89, 0x35, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B,
        0x4E, 0x08, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x78
    };
    constexpr unsigned char runScriptPattern[] = {
        0x48, 0x89, 0x5C, 0x24, 0x00, 0x4C, 0x89, 0x4C, 0x24, 0x00,
        0x00, 0x89, 0x00, 0x24, 0x00, 0x55, 0x56, 0x57, 0x41, 0x54,
        0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C
    };
    constexpr unsigned char makeSymbolPattern[] = {
        0x40, 0x55, 0x56, 0x48, 0x83, 0xEC, 0x00, 0x48, 0x63
    };
    constexpr unsigned char getAttributePattern[] = {
        0x12, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x8B, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x00, 0x80, 0x38,
        0x00, 0x74, 0x00, 0x48, 0x8D, 0x4C
    };
    constexpr unsigned char setAttributePattern[] = {
        0xFF, 0x90, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83,
        0xC6, 0x00, 0x48, 0x3B, 0x00, 0x75, 0x00, 0x4C
    };
    const PatternResult mainMenu = ScanExecutableSections(
        client, mainMenuPattern, "xxx????xxxxxxxxxx");
    const PatternResult runScript = ScanExecutableSections(
        panorama, runScriptPattern, "xxxx?xxxx??x?x?xxxxxxxxxxxxxx");
    const PatternResult makeSymbol = ScanExecutableSections(
        panorama, makeSymbolPattern, "xxxxxx?xx");
    const PatternResult getAttribute = ScanExecutableSections(
        client, getAttributePattern, "xxxxxx????xx?xxxx?xxxx?xxx");
    const PatternResult setAttribute = ScanExecutableSections(
        client, setAttributePattern, "xx????xxx?xx?x?x");
    std::int32_t getAttributeOffset = 0;
    std::int32_t setAttributeOffset = 0;
    const bool attributeOffsetsValid = getAttribute.count == 1 &&
        setAttribute.count == 1 &&
        ReadInteger32(getAttribute.first + 6, getAttributeOffset) &&
        ReadInteger32(setAttribute.first + 2, setAttributeOffset) &&
        getAttributeOffset > 0 && getAttributeOffset < 0x2000 &&
        setAttributeOffset > 0 && setAttributeOffset < 0x2000 &&
        getAttributeOffset % sizeof(uintptr_t) == 0 &&
        setAttributeOffset % sizeof(uintptr_t) == 0;
    if (mainMenu.count != 1 || runScript.count != 1 ||
        makeSymbol.count != 1 || !attributeOffsetsValid) {
        AppendLog("Panorama mount: firmas incompatibles; cancelado.");
        return false;
    }

    const uintptr_t mainMenuGlobal = ResolveRelative(mainMenu.first, 3);
    if (!mainMenuGlobal) {
        AppendLog("Panorama mount: root no resoluble; cancelado.");
        return false;
    }

    g_uiEngine = reinterpret_cast<void*>(uiEngine);
    g_mainMenuGlobal = mainMenuGlobal;
    g_runScript = reinterpret_cast<RunScriptFn>(runScript.first);
    g_makeSymbol = reinterpret_cast<MakeSymbolFn>(makeSymbol.first);
    g_getAttributeStringOffset = getAttributeOffset;
    g_setAttributeStringOffset = setAttributeOffset;
    if (!ResolveRootPanel()) {
        ShutdownPanoramaMount();
        AppendLog("Panorama mount: root no valido; cancelado.");
        return false;
    }
    InterlockedExchange(&g_initialized, 1);
    InterlockedExchange(&g_pendingAction, static_cast<LONG>(
        PendingAction::InstallNativeLoadoutObserver));
    AppendLog("Panorama mount: ABI, root y command bridge listos.");
    return true;
}

bool IsPanoramaProbeEnabled() noexcept {
    wchar_t tempPath[MAX_PATH]{};
    if (!GetTempPathW(_countof(tempPath), tempPath)) return false;
    wchar_t markerPath[MAX_PATH]{};
    if (FAILED(StringCchPrintfW(markerPath, _countof(markerPath),
        L"%sOverlayAI.PanoramaProbe.enable", tempPath)))
        return false;
    return GetFileAttributesW(markerPath) != INVALID_FILE_ATTRIBUTES;
}

void PublishPanoramaInventorySnapshot(const char* json) noexcept {
    RevealSnapshot next{};
    (void)FindPendingItem(json, next);
    if (next.available) next.hash = HashReveal(next);
    if (next.available)
        InterlockedExchange(&g_collectionVisible, 0);

    bool debugPanoramaUi = false;
    (void)ReadJsonBool(json, "use_debug_panorama_ui", debugPanoramaUi);
    const LONG debugUiValue = debugPanoramaUi ? 1 : 0;
    const bool debugUiChanged = InterlockedCompareExchange(
        &g_useDebugPanoramaUi, 0, 0) != debugUiValue;
    if (debugUiChanged)
        InterlockedExchange(&g_useDebugPanoramaUi, debugUiValue);

    AcquireSRWLockExclusive(&g_revealLock);
    const bool changed = next.available != g_reveal.available ||
        next.hash != g_reveal.hash || next.localId != g_reveal.localId;
    g_reveal = next;
    const bool collectionAvailable = g_collection.available &&
        InterlockedCompareExchange(&g_collectionVisible, 0, 0) != 0;
    ReleaseSRWLockExclusive(&g_revealLock);

    if ((!changed && !debugUiChanged) ||
        InterlockedCompareExchange(&g_initialized, 0, 0) == 0)
        return;
    // Install the native inventory observer without opening a popup here.
    const PendingAction action = collectionAvailable
        ? PendingAction::RenderCollection
        : next.available ? PendingAction::RenderReveal
                         : PendingAction::InstallNativeLoadoutObserver;
    InterlockedExchange(&g_pendingAction, static_cast<LONG>(action));
}

bool ConsumePanoramaInventoryCommand(
    PanoramaInventoryCommand& command) noexcept {
    command = {};
    const LONG type = InterlockedExchange(&g_pendingCommand,
        static_cast<LONG>(PanoramaInventoryCommandType::None));
    if (type != static_cast<LONG>(PanoramaInventoryCommandType::Equip) &&
        type != static_cast<LONG>(PanoramaInventoryCommandType::CloseReveal) &&
        type != static_cast<LONG>(
            PanoramaInventoryCommandType::NativeEquipItem))
        return false;
    command.type = static_cast<PanoramaInventoryCommandType>(type);
    command.localId = static_cast<std::uint64_t>(InterlockedExchange64(
        &g_pendingCommandLocalId, 0));
    return command.localId != 0;
}

void RequeuePanoramaInventoryCommand(
    const PanoramaInventoryCommand& command) noexcept {
    if (command.type == PanoramaInventoryCommandType::None ||
        command.localId == 0)
        return;
    InterlockedExchange64(&g_pendingCommandLocalId,
        static_cast<LONG64>(command.localId));
    InterlockedExchange(&g_pendingCommand,
        static_cast<LONG>(command.type));
}

void RequestPanoramaRevealAcknowledged(std::uint64_t localId) noexcept {
    if (localId == 0) return;
    if (InterlockedCompareExchange(&g_pendingCommand,
            static_cast<LONG>(PanoramaInventoryCommandType::CloseReveal),
            static_cast<LONG>(PanoramaInventoryCommandType::None)) !=
        static_cast<LONG>(PanoramaInventoryCommandType::None))
        return;
    InterlockedExchange64(&g_pendingCommandLocalId,
        static_cast<LONG64>(localId));
}

void RequestPanoramaRevealRefresh() noexcept {
    if (InterlockedCompareExchange(&g_initialized, 0, 0) == 0)
        return;
    AcquireSRWLockShared(&g_revealLock);
    const bool available = g_reveal.available;
    ReleaseSRWLockShared(&g_revealLock);
    if (available)
        InterlockedExchange(&g_pendingAction,
            static_cast<LONG>(PendingAction::RenderReveal));
}

void RequestPanoramaCollection(std::uint64_t localId) noexcept {
    if (localId == 0 ||
        InterlockedCompareExchange(&g_initialized, 0, 0) == 0)
        return;
    bool copied = false;
    AcquireSRWLockExclusive(&g_revealLock);
    if (g_reveal.available && g_reveal.localId == localId) {
        g_collection = g_reveal;
        copied = true;
    }
    ReleaseSRWLockExclusive(&g_revealLock);
    if (!copied) return;
    InterlockedExchange(&g_collectionVisible, 1);
    InterlockedExchange(&g_pendingAction,
        static_cast<LONG>(PendingAction::RenderCollection));
}

void RequestPanoramaProbeCreate() noexcept {
    if (InterlockedCompareExchange(&g_initialized, 0, 0) != 0)
        InterlockedExchange(&g_pendingAction,
            static_cast<LONG>(PendingAction::Create));
}

void RequestPanoramaProbeDestroy() noexcept {
    if (InterlockedCompareExchange(&g_initialized, 0, 0) != 0 &&
        InterlockedCompareExchange(&g_mounted, 0, 0) != 0)
        InterlockedExchange(&g_pendingAction,
            static_cast<LONG>(PendingAction::Destroy));
}

void RunPanoramaMountFrame() noexcept {
    PollPanoramaCommand();
    const auto action = static_cast<PendingAction>(InterlockedExchange(
        &g_pendingAction, static_cast<LONG>(PendingAction::None)));
    if (action == PendingAction::None) return;

    if (action != PendingAction::Destroy &&
        action != PendingAction::InstallNativeLoadoutObserver)
        (void)ExecuteScript(kNativeLoadoutObserverScript);

    if (action == PendingAction::Create) {
        if (ExecuteScript(kCreateScript)) {
            InterlockedExchange(&g_mounted, 1);
            g_mountedRootPanel = g_lastScriptRootPanel;
            AppendLog("Panorama mount: panel creado en game thread.");
        } else {
            InterlockedExchange(&g_mounted, 0);
            AppendLog("Panorama mount: creacion rechazada de forma segura.");
        }
        return;
    }

    if (action == PendingAction::InstallNativeLoadoutObserver) {
        if (ExecuteScript(kNativeLoadoutObserverScript)) {
            InterlockedExchange(&g_mounted, 1);
            g_mountedRootPanel = g_lastScriptRootPanel;
            AppendLog("Panorama loadout: observador nativo instalado.");
        } else {
            InterlockedExchange(&g_mounted, 0);
            AppendLog("Panorama loadout: observador nativo rechazado.");
        }
        return;
    }

    if (action == PendingAction::RenderReveal) {
        const bool debugUi = InterlockedCompareExchange(
            &g_useDebugPanoramaUi, 0, 0) != 0;
        RevealSnapshot reveal{};
        AcquireSRWLockShared(&g_revealLock);
        reveal = g_reveal;
        ReleaseSRWLockShared(&g_revealLock);

        if (!debugUi) {
            char nativeLocalId[32]{};
            StringCchPrintfA(nativeLocalId, _countof(nativeLocalId), "%llu",
                static_cast<unsigned long long>(reveal.localId));
            std::string observerScript = kNativeRevealObserverScript;
            ReplaceScriptToken(observerScript, "__LOCAL_ID__", nativeLocalId);
            if (reveal.available && ExecuteScript(observerScript.c_str())) {
                InterlockedExchange(&g_mounted, 1);
                g_mountedRootPanel = g_lastScriptRootPanel;
                AppendLog(
                    "Panorama reveal: observador nativo instalado.");
            } else if (!reveal.available &&
                ExecuteScript(kNativeLoadoutObserverScript)) {
                InterlockedExchange(&g_mounted, 1);
                g_mountedRootPanel = g_lastScriptRootPanel;
                AppendLog("Panorama reveal: observador de loadout conservado.");
            } else {
                InterlockedExchange(&g_mounted, 0);
                AppendLog(
                    "Panorama reveal: observador nativo rechazado.");
            }
            return;
        }

        std::uint64_t generatedItemId = 0;
        if (reveal.available && !ResolveInventorySocacheGeneratedItemId(
                reveal.localId, generatedItemId)) {
            const ULONGLONG now = GetTickCount64();
            if (g_revealIdentityWaitLocalId != reveal.localId) {
                g_revealIdentityWaitLocalId = reveal.localId;
                g_revealIdentityWaitStartedAt = now;
                g_nextRevealIdentityRetryAt = 0;
                AppendLog(
                    "Panorama reveal nativo: esperando identidad SOCache.");
            }
            if (now - g_revealIdentityWaitStartedAt < 3000) {
                if (now >= g_nextRevealIdentityRetryAt)
                    g_nextRevealIdentityRetryAt = now + 50;
                InterlockedExchange(&g_pendingAction,
                    static_cast<LONG>(PendingAction::RenderReveal));
                return;
            }
            AppendLog(
                "Panorama reveal nativo: identidad no disponible; fallback general.");
        } else {
            g_revealIdentityWaitLocalId = 0;
            g_revealIdentityWaitStartedAt = 0;
            g_nextRevealIdentityRetryAt = 0;
        }
        char localIdText[32]{};
        char itemIdText[32]{};
        char countText[16]{};
        StringCchPrintfA(localIdText, _countof(localIdText), "%llu",
            static_cast<unsigned long long>(reveal.localId));
        StringCchPrintfA(itemIdText, _countof(itemIdText), "%llu",
            static_cast<unsigned long long>(generatedItemId));
        StringCchPrintfA(countText, _countof(countText), "%u",
            static_cast<unsigned int>(reveal.pendingCount));
        std::string script = kQueuedRevealScriptTemplate;
        ReplaceScriptToken(script, "__ITEMS__", reveal.itemsJson);
        ReplaceScriptToken(script, "__COUNT__", countText);
        ReplaceScriptToken(script, "__LOCAL_ID__", localIdText);
        ReplaceScriptToken(script, "__ITEM_ID__", itemIdText);
        if (reveal.available && ExecuteScript(script.c_str())) {
            InterlockedExchange(&g_mounted, 1);
            g_mountedRootPanel = g_lastScriptRootPanel;
            AppendLog(
                "Panorama reveal: cola unica instalada para Inventario.");
        } else {
            InterlockedExchange(&g_mounted, 0);
            AppendLog(
                "Panorama reveal: montaje rechazado de forma segura.");
        }
        return;
    }

    if (action == PendingAction::RenderCollection) {
        if (InterlockedCompareExchange(&g_useDebugPanoramaUi, 0, 0) == 0) {
            InterlockedExchange(&g_collectionVisible, 0);
            std::string observerScript = kNativeRevealObserverScript;
            ReplaceScriptToken(observerScript, "__LOCAL_ID__", "0");
            if (ExecuteScript(observerScript.c_str())) {
                InterlockedExchange(&g_mounted, 1);
                g_mountedRootPanel = g_lastScriptRootPanel;
            }
            AppendLog(
                "Panorama collection: popup debug omitido (modo nativo).");
            return;
        }

        RevealSnapshot collection{};
        AcquireSRWLockShared(&g_revealLock);
        collection = g_collection;
        ReleaseSRWLockShared(&g_revealLock);
        char script[16384]{};
        const HRESULT formatted = collection.available
            ? StringCchPrintfA(script, _countof(script), kCollectionScriptFormat,
                collection.itemJson, collection.equipped ? "true" : "false")
            : E_FAIL;
        if (SUCCEEDED(formatted) && ExecuteScript(script)) {
            InterlockedExchange(&g_mounted, 1);
            g_mountedRootPanel = g_lastScriptRootPanel;
            AppendLog("Panorama collection: articulo mostrado en game thread.");
        } else {
            InterlockedExchange(&g_mounted, 0);
            AppendLog("Panorama collection: render rechazado de forma segura.");
        }
        return;
    }

    if (ExecuteScript(kDestroyScript))
        AppendLog("Panorama mount: panel destruido en game thread.");
    else
        AppendLog("Panorama mount: destruccion no disponible.");
    InterlockedExchange(&g_mounted, 0);
    g_mountedRootPanel = 0;
}

bool IsPanoramaProbeMounted() noexcept {
    return InterlockedCompareExchange(&g_mounted, 0, 0) != 0;
}

void ShutdownPanoramaMount() noexcept {
    InterlockedExchange(&g_pendingAction,
        static_cast<LONG>(PendingAction::None));
    InterlockedExchange(&g_mounted, 0);
    InterlockedExchange(&g_initialized, 0);
    InterlockedExchange(&g_collectionVisible, 0);
    InterlockedExchange(&g_useDebugPanoramaUi, 0);
    g_runScript = nullptr;
    g_makeSymbol = nullptr;
    g_getAttributeStringOffset = 0;
    g_setAttributeStringOffset = 0;
    g_commandSymbol = -1;
    g_actionSymbol = -1;
    InterlockedExchange(&g_commandAccessReady, 0);
    InterlockedExchange(&g_pendingCommand, 0);
    InterlockedExchange64(&g_pendingCommandLocalId, 0);
    g_nextCommandPollAt = 0;
    g_lastScriptRootPanel = 0;
    g_mountedRootPanel = 0;
    g_mainMenuGlobal = 0;
    g_uiEngine = nullptr;
    AcquireSRWLockExclusive(&g_revealLock);
    g_reveal = {};
    g_collection = {};
    ReleaseSRWLockExclusive(&g_revealLock);
}
