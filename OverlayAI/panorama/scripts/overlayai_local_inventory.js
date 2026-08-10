"use strict";

var OverlayAILocalInventory = OverlayAILocalInventory || {};

(function (api) {
    var FRONTEND_ABI_VERSION = 2;
    var PROTOCOL_VERSION = 1;
    var requestId = 1;
    var selectedLocalId = 0;
    var dismissedRevealId = 0;
    var removeArmedId = 0;
    var categoryFilter = -1;
    var searchQuery = "";
    var bridgeReady = false;
    var helloSent = false;
    var mounted = false;
    var viewMode = "collection";
    var refreshScheduleHandle = null;
    var REFRESH_INTERVAL_SECONDS = 1.25;
    var snapshot = {
        items: [],
        loadout: { music_kit: 0 },
        pending_reveal_item_id: 0,
        selected_local_id: 0
    };
    var clientSessionId = createSessionId();

    function createSessionId() {
        var timestamp = new Date().getTime().toString(36);
        var randomPart = Math.floor(Math.random() * 0x7fffffff).toString(36);
        return "overlayai-panorama-" + timestamp + "-" + randomPart;
    }

    function findPanel(id) {
        var context = $.GetContextPanel();
        if (!context) {
            return null;
        }
        if (context.id === id) {
            return context;
        }
        if (typeof context.FindChildTraverse === "function") {
            return context.FindChildTraverse(id);
        }
        return context.FindChildInLayoutFile(id);
    }

    function setText(id, value) {
        var panel = findPanel(id);
        if (panel) {
            panel.text = String(value);
        }
    }

    function setButtonEnabled(id, enabled) {
        var button = findPanel(id);
        if (!button) {
            return;
        }
        button.SetHasClass("Disabled", !enabled);
        button.enabled = enabled;
    }

    function setImage(id, imageUrl) {
        var image = findPanel(id);
        if (!image) {
            return;
        }
        if (imageUrl) {
            image.SetImage(String(imageUrl));
            image.SetHasClass("HasImage", true);
        } else {
            image.SetHasClass("HasImage", false);
        }
    }

    function colorToCss(value) {
        var color = Number(value || 0) >>> 0;
        var text = color.toString(16);
        while (text.length < 6) {
            text = "0" + text;
        }
        return "#" + text.slice(-6);
    }

    function applyRarityColor(id, item) {
        var panel = findPanel(id);
        if (panel && panel.style) {
            panel.style.backgroundColor = colorToCss(item ? item.rarity_color : 0x4b69ff);
        }
    }

    function nextRequestId() {
        var current = requestId;
        requestId += 1;
        return current;
    }

    function send(messageType, payload) {
        if (!bridgeReady || typeof OverlayAIPanoramaBridgeSend !== "function") {
            $.Msg("[OverlayAI] Panorama bridge unavailable: " + messageType);
            return false;
        }

        OverlayAIPanoramaBridgeSend(JSON.stringify({
            protocol_version: PROTOCOL_VERSION,
            message_type: messageType,
            request_id: nextRequestId(),
            payload: payload || {}
        }));
        return true;
    }

    function beginSession() {
        if (!bridgeReady || helloSent) {
            return;
        }
        helloSent = send("client.hello", {
            client_session_id: clientSessionId
        });
    }

    function cancelRefreshPoll() {
        if (refreshScheduleHandle !== null && typeof $.CancelScheduled === "function") {
            $.CancelScheduled(refreshScheduleHandle);
        }
        refreshScheduleHandle = null;
    }

    function scheduleRefreshPoll() {
        cancelRefreshPoll();
        if (!mounted || !bridgeReady || typeof $.Schedule !== "function") {
            return;
        }
        refreshScheduleHandle = $.Schedule(REFRESH_INTERVAL_SECONDS, function () {
            refreshScheduleHandle = null;
            if (!mounted || !bridgeReady) {
                return;
            }
            send("inventory.request_refresh", {});
            scheduleRefreshPoll();
        });
    }

    function normalizeItems(payload) {
        var source = payload && payload.items &&
            typeof payload.items.length === "number" ? payload.items : [];
        var result = [];
        var knownIds = {};

        for (var index = 0; index < source.length && result.length < 256; index += 1) {
            var item = source[index];
            var localId = Number(item && item.local_id || 0);
            var itemType = Number(item && item.type);
            if (!item || itemType < 0 || itemType > 4 || localId <= 0 || knownIds[localId]) {
                continue;
            }
            knownIds[localId] = true;
            result.push(item);
        }

        result.sort(function (left, right) {
            return Number(right.acquired_at || 0) - Number(left.acquired_at || 0);
        });
        return result;
    }

    function itemTypeName(item) {
        var names = ["MUSIC KIT", "WEAPON SKIN", "KNIFE", "GLOVES", "AGENT",
            "COLLECTIBLE", "CASE / CONTAINER", "KEY", "STICKER"];
        return names[Number(item && item.type)] || "LOCAL ITEM";
    }

    function visibleItems() {
        var result = [];
        for (var index = 0; index < snapshot.items.length; index += 1) {
            var item = snapshot.items[index];
            if (categoryFilter >= 0 && Number(item.type) !== categoryFilter) continue;
            var haystack = (itemName(item) + " " + itemTypeName(item) + " " +
                String(item.group || "") + " " + String(item.rarity || "")).toLowerCase();
            if (searchQuery && haystack.indexOf(searchQuery) < 0) continue;
            result.push(item);
        }
        return result;
    }

    function findItem(localId) {
        for (var index = 0; index < snapshot.items.length; index += 1) {
            if (Number(snapshot.items[index].local_id) === Number(localId)) {
                return snapshot.items[index];
            }
        }
        return null;
    }

    function isEquipped(localId) {
        return Number(snapshot.loadout && snapshot.loadout.music_kit || 0) === Number(localId);
    }

    function itemName(item) {
        return item && (item.custom_name || item.display_name) || "Music Kit local";
    }

    function itemRarity(item) {
        return String(item && item.rarity || "High Grade").toUpperCase();
    }

    function chooseSelection(payload) {
        var pendingId = Number(payload && payload.pending_reveal_item_id || 0);
        var stateSelectedId = Number(payload && payload.selected_local_id || 0);
        if (findItem(selectedLocalId)) {
            return selectedLocalId;
        }
        if (findItem(pendingId)) {
            return pendingId;
        }
        if (findItem(stateSelectedId)) {
            return stateSelectedId;
        }
        return snapshot.items.length > 0 ? Number(snapshot.items[0].local_id) : 0;
    }

    function showView(mode) {
        viewMode = mode;
        var root = findPanel("OverlayAILocalInventoryRoot");
        if (!root) {
            return;
        }
        root.SetHasClass("RevealMode", mode === "reveal");
        root.SetHasClass("CollectionMode", mode === "collection");
    }

    function renderReveal(item) {
        if (!item) {
            showView("collection");
            return;
        }

        selectedLocalId = Number(item.local_id);
        var equipped = isEquipped(selectedLocalId);
        var canEquip = Number(item.type) === 0 && String(item.validity) === "Valido";
        setText("OverlayAIRevealItemType", itemTypeName(item));
        setText("OverlayAIRevealItemName", itemName(item));
        setText("OverlayAIRevealItemRarity", itemRarity(item));
        setText("OverlayAIRevealItemStatus", equipped
            ? "Equipado localmente. Puedes continuar hacia tu coleccion."
            : "Nuevo articulo local listo para equiparse o guardarse en la coleccion.");
        setText("OverlayAIRevealItemIdentity", "LOCAL ID " + selectedLocalId);
        setText("OverlayAIRevealEquipLabel", equipped ? "EQUIPADO" :
            (canEquip ? "EQUIPAR LOCALMENTE" : "SOLO VISUAL"));
        setButtonEnabled("OverlayAIRevealEquipButton", canEquip && !equipped);
        setImage("OverlayAIRevealItemImage", item.image_url);
        applyRarityColor("OverlayAIRevealRarityLine", item);
        showView("reveal");
    }

    function createLabel(parent, className, text) {
        var label = $.CreatePanel("Label", parent, "");
        label.AddClass(className);
        label.text = String(text || "");
        return label;
    }

    function bindSelection(panel, localId) {
        panel.SetPanelEvent("onactivate", function () {
            selectedLocalId = localId;
            removeArmedId = 0;
            renderCollection();
        });
    }

    function renderCollectionCards() {
        var list = findPanel("OverlayAICollectionList");
        if (!list || typeof $.CreatePanel !== "function") {
            return;
        }
        list.RemoveAndDeleteChildren();

        var items = visibleItems();
        for (var index = 0; index < items.length; index += 1) {
            var item = items[index];
            var localId = Number(item.local_id);
            var card = $.CreatePanel("Button", list, "OverlayAICollectionCard_" + localId);
            card.AddClass("OverlayAICollectionCard");
            card.SetHasClass("Selected", localId === selectedLocalId);
            card.SetHasClass("Equipped", isEquipped(localId));
            bindSelection(card, localId);

            var image = $.CreatePanel("Image", card, "");
            image.AddClass("OverlayAICollectionCardImage");
            image.SetHasClass("HasImage", !!item.image_url);
            if (item.image_url) {
                image.SetImage(String(item.image_url));
            }

            var copy = $.CreatePanel("Panel", card, "");
            copy.AddClass("OverlayAICollectionCardCopy");
            createLabel(copy, "OverlayAICollectionCardName", itemName(item));
            createLabel(copy, "OverlayAICollectionCardRarity",
                itemTypeName(item) + "  //  " + itemRarity(item));
            createLabel(copy, "OverlayAICollectionCardIdentity", "LOCAL ID " + localId);

            var marker = $.CreatePanel("Panel", card, "");
            marker.AddClass("OverlayAICollectionCardMarker");
            if (marker.style) {
                marker.style.backgroundColor = colorToCss(item.rarity_color);
            }
        }
    }

    function renderCollectionDetail() {
        var item = findItem(selectedLocalId);
        var equipped = item && isEquipped(selectedLocalId);
        if (!item) {
            setText("OverlayAICollectionItemName", "Coleccion local vacia");
            setText("OverlayAICollectionItemType", "LOCAL ITEM");
            setText("OverlayAICollectionItemRarity", "SIN RESULTADOS");
            setText("OverlayAICollectionItemMeta", "Cambia los filtros o anade un articulo desde OverlayAI.");
            setText("OverlayAICollectionItemIdentity", "SIN SELECCION");
            setImage("OverlayAICollectionItemImage", "");
            setButtonEnabled("OverlayAICollectionEquipButton", false);
            setButtonEnabled("OverlayAICollectionUnequipButton", false);
            setButtonEnabled("OverlayAICollectionDuplicateButton", false);
            setButtonEnabled("OverlayAICollectionRemoveButton", false);
            return;
        }

        var validItem = String(item.validity) === "Valido";
        var canEquip = Number(item.type) === 0 && validItem;
        var metadata = String(item.group || itemTypeName(item));
        if (Number(item.type) >= 1 && Number(item.type) <= 3) {
            metadata += "  //  Wear " + Number(item.wear || 0).toFixed(6) +
                "  //  Seed " + Number(item.seed || 0);
        }
        if (item.stattrak) metadata += "  //  StatTrak " + Number(item.stattrak_count || 0);
        if (item.souvenir) metadata += "  //  Souvenir";
        if (!validItem) metadata += "  //  " + String(item.validity || "Invalido");
        setText("OverlayAICollectionItemType", itemTypeName(item));
        setText("OverlayAICollectionItemName", itemName(item));
        setText("OverlayAICollectionItemRarity", itemRarity(item));
        setText("OverlayAICollectionItemMeta", metadata + (equipped
            ? "\nEquipado localmente."
            : (canEquip ? "\nDisponible para equipar localmente."
                        : "\nAplicacion reservada para una fase posterior.")));
        setText("OverlayAICollectionItemIdentity", "LOCAL ID " + selectedLocalId);
        setImage("OverlayAICollectionItemImage", item.image_url);
        applyRarityColor("OverlayAICollectionRarityLine", item);
        setButtonEnabled("OverlayAICollectionEquipButton", canEquip && !equipped);
        setButtonEnabled("OverlayAICollectionUnequipButton", canEquip && !!equipped);
        setButtonEnabled("OverlayAICollectionDuplicateButton", validItem);
        setButtonEnabled("OverlayAICollectionRemoveButton", true);
        setText("OverlayAICollectionRemoveLabel",
            removeArmedId === selectedLocalId ? "CONFIRMAR" : "ELIMINAR");
    }

    function renderCollection() {
        var items = visibleItems();
        var selectedVisible = false;
        for (var index = 0; index < items.length; index += 1) {
            selectedVisible = selectedVisible || Number(items[index].local_id) === selectedLocalId;
        }
        if (!selectedVisible) {
            selectedLocalId = items.length > 0 ? Number(items[0].local_id) : 0;
            removeArmedId = 0;
        }
        setText("OverlayAICollectionCount", items.length + " / " + snapshot.items.length + " ITEMS");
        renderCollectionCards();
        renderCollectionDetail();
        showView("collection");
    }

    function handleSnapshot(message) {
        var payload = message && message.payload ? message.payload : {};
        snapshot = {
            items: normalizeItems(payload),
            loadout: payload.loadout || { music_kit: 0 },
            pending_reveal_item_id: Number(payload.pending_reveal_item_id || 0),
            selected_local_id: Number(payload.selected_local_id || 0)
        };
        selectedLocalId = chooseSelection(payload);
        setText("OverlayAIBridgeState", "CONNECTED");
        setText("OverlayAICollectionBridgeState", "CONNECTED");

        var pendingItem = findItem(snapshot.pending_reveal_item_id);
        if (pendingItem && dismissedRevealId !== snapshot.pending_reveal_item_id) {
            renderReveal(pendingItem);
        } else {
            renderCollection();
        }
    }

    function requestRefresh() {
        if (send("inventory.request_refresh", {})) {
            setText("OverlayAICollectionStatus", "Actualizando coleccion...");
        }
    }

    function equipSelected() {
        var item = findItem(selectedLocalId);
        if (!item || Number(item.type) !== 0 || isEquipped(selectedLocalId)) {
            return;
        }
        if (send("inventory.equip", { local_id: selectedLocalId })) {
            setText("OverlayAIRevealItemStatus", "Solicitud de equipamiento enviada...");
            setText("OverlayAICollectionStatus", "Equipando Music Kit...");
        }
    }

    function unequipSelected() {
        var item = findItem(selectedLocalId);
        if (!item || Number(item.type) !== 0 || !isEquipped(selectedLocalId)) {
            return;
        }
        if (send("inventory.unequip", {})) {
            setText("OverlayAICollectionStatus", "Restaurando Music Kit anterior...");
        }
    }

    function duplicateSelected() {
        if (selectedLocalId === 0) return;
        if (send("inventory.duplicate", { local_id: selectedLocalId })) {
            setText("OverlayAICollectionStatus", "Duplicando variante local...");
        }
    }

    function removeSelected() {
        if (selectedLocalId === 0) return;
        if (removeArmedId !== selectedLocalId) {
            removeArmedId = selectedLocalId;
            setText("OverlayAICollectionRemoveLabel", "CONFIRMAR");
            setText("OverlayAICollectionStatus", "Pulsa CONFIRMAR para eliminar esta instancia local.");
            return;
        }
        var removingId = selectedLocalId;
        removeArmedId = 0;
        if (send("inventory.remove", { local_id: removingId })) {
            setText("OverlayAICollectionStatus", "Eliminando instancia local...");
        }
    }

    function setCategory(type) {
        categoryFilter = type;
        removeArmedId = 0;
        var ids = ["All", "Music", "Weapons", "Knives", "Gloves", "Agents"];
        for (var index = 0; index < ids.length; index += 1) {
            var button = findPanel("OverlayAIFilter" + ids[index]);
            if (button) button.SetHasClass("Selected", index - 1 === categoryFilter);
        }
        renderCollection();
    }

    function updateSearch() {
        var entry = findPanel("OverlayAICollectionSearch");
        searchQuery = String(entry && entry.text || "").toLowerCase();
        removeArmedId = 0;
        renderCollection();
    }

    function continueReveal() {
        var revealId = Number(snapshot.pending_reveal_item_id || selectedLocalId);
        if (revealId !== 0) {
            dismissedRevealId = revealId;
            send("inventory.close_reveal", { local_id: revealId });
        }
        renderCollection();
    }

    function handleAction(message) {
        var payload = message && message.payload ? message.payload : {};
        var detail = String(payload.detail || "Estado de inventario actualizado.");
        setText("OverlayAIRevealItemStatus", detail);
        setText("OverlayAICollectionStatus", detail);
        if (message.message_type === "inventory.reveal_closed") {
            dismissedRevealId = Number(payload.local_id || dismissedRevealId);
            showView("collection");
        }
        if (message.message_type === "inventory.item_removed") {
            selectedLocalId = 0;
            removeArmedId = 0;
        }
        if (message.message_type === "inventory.item_added") {
            selectedLocalId = Number(payload.local_id || 0);
        }
        requestRefresh();
    }

    api.GetFrontendAbiVersion = function () {
        return FRONTEND_ABI_VERSION;
    };

    api.SetBridgeReady = function (ready) {
        bridgeReady = ready === true;
        helloSent = false;
        setText("OverlayAIBridgeState", bridgeReady ? "BRIDGE READY" : "DISCONNECTED");
        setText("OverlayAICollectionBridgeState", bridgeReady ? "BRIDGE READY" : "DISCONNECTED");
        if (!bridgeReady) {
            cancelRefreshPoll();
            setText("OverlayAICollectionStatus", "Puente desconectado. La coleccion visible se conserva.");
            return;
        }
        beginSession();
        scheduleRefreshPoll();
    };

    api.ApplyMessage = function (serializedMessage) {
        var message;
        try {
            message = JSON.parse(serializedMessage);
        } catch (error) {
            $.Warning("[OverlayAI] Invalid bridge message: " + error);
            return false;
        }

        if (!message || Number(message.protocol_version) !== PROTOCOL_VERSION ||
            typeof message.message_type !== "string" || !message.payload) {
            $.Warning("[OverlayAI] Unsupported or malformed inventory message.");
            return false;
        }
        if (message.message_type === "inventory.snapshot") {
            handleSnapshot(message);
            return true;
        }
        if (message.message_type === "protocol.error") {
            var errorPayload = message.payload || {};
            var errorText = "Error de protocolo: " + String(errorPayload.message || "desconocido");
            setText("OverlayAIRevealItemStatus", errorText);
            setText("OverlayAICollectionStatus", errorText);
            return true;
        }
        if (message.message_type.indexOf("inventory.") === 0) {
            handleAction(message);
            return true;
        }
        return false;
    };

    api.Mount = function () {
        if (mounted) {
            return true;
        }
        var root = findPanel("OverlayAILocalInventoryRoot");
        if (!root) {
            return false;
        }
        mounted = true;
        root.AddClass("Mounted");
        showView("collection");

        var bindings = [
            ["OverlayAIRevealEquipButton", equipSelected],
            ["OverlayAIContinueButton", continueReveal],
            ["OverlayAICollectionEquipButton", equipSelected],
            ["OverlayAICollectionUnequipButton", unequipSelected],
            ["OverlayAICollectionDuplicateButton", duplicateSelected],
            ["OverlayAICollectionRemoveButton", removeSelected],
            ["OverlayAICollectionRefreshButton", requestRefresh],
            ["OverlayAICollectionCloseButton", api.Destroy],
            ["OverlayAIFilterAll", function () { setCategory(-1); }],
            ["OverlayAIFilterMusic", function () { setCategory(0); }],
            ["OverlayAIFilterWeapons", function () { setCategory(1); }],
            ["OverlayAIFilterKnives", function () { setCategory(2); }],
            ["OverlayAIFilterGloves", function () { setCategory(3); }],
            ["OverlayAIFilterAgents", function () { setCategory(4); }]
        ];
        for (var index = 0; index < bindings.length; index += 1) {
            var panel = findPanel(bindings[index][0]);
            if (panel) {
                panel.SetPanelEvent("onactivate", bindings[index][1]);
            }
        }

        setButtonEnabled("OverlayAIRevealEquipButton", false);
        setButtonEnabled("OverlayAICollectionEquipButton", false);
        setButtonEnabled("OverlayAICollectionUnequipButton", false);
        setButtonEnabled("OverlayAICollectionDuplicateButton", false);
        setButtonEnabled("OverlayAICollectionRemoveButton", false);
        var search = findPanel("OverlayAICollectionSearch");
        if (search) search.SetPanelEvent("ontextchanged", updateSearch);
        setCategory(-1);
        beginSession();
        return true;
    };

    api.RequestRefresh = function () {
        requestRefresh();
    };

    api.Unmount = function () {
        if (!mounted) {
            return;
        }
        var root = findPanel("OverlayAILocalInventoryRoot");
        if (root) {
            root.RemoveClass("Mounted");
            root.RemoveClass("RevealMode");
            root.RemoveClass("CollectionMode");
        }
        selectedLocalId = 0;
        cancelRefreshPoll();
        bridgeReady = false;
        helloSent = false;
        mounted = false;
    };

    api.Destroy = function () {
        var root = findPanel("OverlayAILocalInventoryRoot");
        api.Unmount();
        if (root) {
            root.DeleteAsync(0.0);
        }
    };

    api.Mount();
})(OverlayAILocalInventory);
