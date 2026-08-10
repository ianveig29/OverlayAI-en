"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

const panels = {};

class MockPanel {
    constructor(id, parent) {
        this.id = id || "";
        this.parent = parent || null;
        this.children = [];
        this.text = "";
        this.enabled = true;
        this.visible = true;
        this.classes = new Set();
        this.events = {};
        this.deleted = false;
        this.image = "";
        this.style = {};
        if (this.parent) this.parent.children.push(this);
        if (this.id) panels[this.id] = this;
    }

    FindChildInLayoutFile(id) { return panels[id] || null; }
    FindChildTraverse(id) { return panels[id] || null; }
    AddClass(name) { this.classes.add(name); }
    RemoveClass(name) { this.classes.delete(name); }
    SetHasClass(name, enabled) { enabled ? this.classes.add(name) : this.classes.delete(name); }
    SetPanelEvent(name, callback) { this.events[name] = callback; }
    SetImage(value) { this.image = value; }
    DeleteAsync() { this.deleted = true; }
    RemoveAndDeleteChildren() {
        const remove = (panel) => {
            for (const child of panel.children) remove(child);
            if (panel.id) delete panels[panel.id];
            panel.deleted = true;
        };
        for (const child of this.children) remove(child);
        this.children = [];
    }
}

const root = new MockPanel("OverlayAILocalInventoryRoot");
const ids = [
    "OverlayAIBridgeState",
    "OverlayAIRevealItemImage",
    "OverlayAIRevealItemType",
    "OverlayAIRevealItemName",
    "OverlayAIRevealItemRarity",
    "OverlayAIRevealItemStatus",
    "OverlayAIRevealEquipButton",
    "OverlayAIRevealEquipLabel",
    "OverlayAIContinueButton",
    "OverlayAIRevealItemIdentity",
    "OverlayAIRevealRarityLine",
    "OverlayAICollectionBridgeState",
    "OverlayAICollectionCount",
    "OverlayAICollectionList",
    "OverlayAICollectionItemImage",
    "OverlayAICollectionItemType",
    "OverlayAICollectionItemName",
    "OverlayAICollectionItemRarity",
    "OverlayAICollectionItemMeta",
    "OverlayAICollectionItemIdentity",
    "OverlayAICollectionRarityLine",
    "OverlayAICollectionEquipButton",
    "OverlayAICollectionUnequipButton",
    "OverlayAICollectionDuplicateButton",
    "OverlayAICollectionRemoveButton",
    "OverlayAICollectionRemoveLabel",
    "OverlayAICollectionRefreshButton",
    "OverlayAICollectionCloseButton",
    "OverlayAICollectionStatus",
    "OverlayAICollectionSearch",
    "OverlayAIFilterAll",
    "OverlayAIFilterMusic",
    "OverlayAIFilterWeapons",
    "OverlayAIFilterKnives",
    "OverlayAIFilterGloves",
    "OverlayAIFilterAgents"
];
for (const id of ids) new MockPanel(id, root);

const outbound = [];
const warnings = [];
const scheduled = new Map();
let nextScheduleHandle = 1;
const context = {
    console,
    JSON,
    Number,
    String,
    Date,
    Math,
    OverlayAIPanoramaBridgeSend(serialized) { outbound.push(JSON.parse(serialized)); },
    $: {
        GetContextPanel() { return root; },
        CreatePanel(type, parent, id) { return new MockPanel(id, parent); },
        Schedule(delay, callback) {
            const handle = nextScheduleHandle++;
            scheduled.set(handle, callback);
            return handle;
        },
        CancelScheduled(handle) { scheduled.delete(handle); },
        Msg() {},
        Warning(message) { warnings.push(String(message)); }
    }
};
vm.createContext(context);

const projectRoot = path.resolve(__dirname, "..");
const frontendPath = path.join(projectRoot, "panorama", "scripts", "overlayai_local_inventory.js");
vm.runInContext(fs.readFileSync(frontendPath, "utf8"), context, { filename: frontendPath });

function check(condition, message) {
    if (!condition) throw new Error(message);
}

function latestOutbound(type) {
    for (let index = outbound.length - 1; index >= 0; index -= 1) {
        if (!type || outbound[index].message_type === type) return outbound[index];
    }
    return null;
}

function makeSnapshot(pendingId, equippedId, selectedId) {
    return {
        protocol_version: 1,
        message_type: "inventory.snapshot",
        request_id: 100,
        payload: {
            selected_local_id: selectedId || 41,
            pending_reveal_item_id: pendingId || 0,
            loadout: { music_kit: equippedId || 0 },
            items: [{
                local_id: 41,
                type: 0,
                display_name: "Music Kit | Test One",
                custom_name: "",
                rarity: "High Grade",
                rarity_color: 0x4b69ff,
                validity: "Valido",
                acquired_at: 10,
                image_url: "file://kit-one.png"
            }, {
                local_id: 42,
                type: 0,
                display_name: "Music Kit | Test Two",
                custom_name: "Mi kit",
                rarity: "Exceptional",
                rarity_color: 0x8847ff,
                validity: "Valido",
                acquired_at: 20,
                image_url: "file://kit-two.png"
            }, {
                local_id: 42,
                type: 0,
                display_name: "Duplicado rechazado",
                acquired_at: 5
            }, {
                local_id: 90,
                type: 1,
                display_name: "AK-47 | Test",
                group: "AK-47",
                rarity: "Classified",
                rarity_color: 0xd32ce6,
                validity: "Valido",
                wear: 0.12,
                seed: 321,
                stattrak: true,
                stattrak_count: 7
            }]
        }
    };
}

function apply(message) {
    check(context.OverlayAILocalInventory.ApplyMessage(JSON.stringify(message)),
        "El frontend rechazo un mensaje valido.");
}

const api = context.OverlayAILocalInventory;
check(api.GetFrontendAbiVersion() === 2, "Frontend ABI incorrecta.");
check(root.classes.has("Mounted"), "El panel no se monto.");
check(root.classes.has("CollectionMode"), "El frontend no inicio en coleccion.");

api.SetBridgeReady(true);
check(outbound.length === 1, "El inicio debe enviar solamente client.hello.");
check(outbound[0].message_type === "client.hello", "Falta client.hello.");
check(/^overlayai-panorama-[a-z0-9-]+$/.test(outbound[0].payload.client_session_id),
    "La sesion del frontend no es unica o valida.");
const originalSessionId = outbound[0].payload.client_session_id;
check(scheduled.size === 1, "El frontend debe programar refresco mientras este conectado.");
const firstPoll = scheduled.values().next().value;
scheduled.clear();
firstPoll();
check(latestOutbound().message_type === "inventory.request_refresh" && scheduled.size === 1,
    "El refresco programado no solicito un snapshot ni se reprogramo.");

apply(makeSnapshot(41, 0, 41));
check(root.classes.has("RevealMode"), "El articulo pendiente no abrio NEW ITEM.");
check(panels.OverlayAIRevealItemName.text === "Music Kit | Test One",
    "El reveal no mostro el articulo pendiente.");
check(panels.OverlayAIRevealEquipButton.enabled, "Equipar debe habilitarse en el reveal.");

panels.OverlayAIRevealEquipButton.events.onactivate();
check(latestOutbound("inventory.equip").payload.local_id === 41,
    "El reveal equipo otro Local ID.");

apply({
    protocol_version: 1,
    message_type: "inventory.loadout_changed",
    request_id: 2,
    payload: { local_id: 41, success: true, detail: "Equipado" }
});
check(latestOutbound().message_type === "inventory.request_refresh",
    "Una accion debe solicitar un snapshot actualizado.");
apply(makeSnapshot(41, 41, 41));
check(!panels.OverlayAIRevealEquipButton.enabled,
    "El Music Kit equipado no debe poder equiparse otra vez.");

panels.OverlayAIContinueButton.events.onactivate();
check(latestOutbound("inventory.close_reveal").payload.local_id === 41,
    "Continuar no cerro el reveal pendiente.");
check(root.classes.has("CollectionMode") && !root.deleted,
    "Continuar debe abrir la coleccion sin destruir el frontend.");

apply({
    protocol_version: 1,
    message_type: "inventory.reveal_closed",
    request_id: 4,
    payload: { local_id: 41, success: true, detail: "Cerrado" }
});
apply(makeSnapshot(0, 41, 41));
check(panels.OverlayAICollectionList.children.length === 3,
    "La coleccion debe mostrar todas las categorias sin duplicados.");
check(panels.OverlayAICollectionCount.text === "3 / 3 ITEMS",
    "El contador de la coleccion es incorrecto.");

panels.OverlayAIFilterMusic.events.onactivate();
check(panels.OverlayAICollectionList.children.length === 2 &&
    panels.OverlayAICollectionCount.text === "2 / 3 ITEMS",
    "El filtro de Music Kits es incorrecto.");
panels.OverlayAIFilterAll.events.onactivate();
panels.OverlayAICollectionSearch.text = "ak-47";
panels.OverlayAICollectionSearch.events.ontextchanged();
check(panels.OverlayAICollectionList.children.length === 1 &&
    panels.OverlayAICollectionCard_90,
    "La busqueda no encontro el arma local.");
panels.OverlayAICollectionSearch.text = "";
panels.OverlayAICollectionSearch.events.ontextchanged();

panels.OverlayAICollectionCard_42.events.onactivate();
check(panels.OverlayAICollectionItemName.text === "Mi kit",
    "Seleccionar una tarjeta no actualizo el detalle.");
check(panels.OverlayAICollectionEquipButton.enabled,
    "El segundo Music Kit debe poder equiparse.");
panels.OverlayAICollectionEquipButton.events.onactivate();
check(latestOutbound("inventory.equip").payload.local_id === 42,
    "La coleccion equipo otro Local ID.");
panels.OverlayAICollectionDuplicateButton.events.onactivate();
check(latestOutbound("inventory.duplicate").payload.local_id === 42,
    "Duplicar uso otro Local ID.");

apply(makeSnapshot(0, 42, 42));
check(!panels.OverlayAICollectionEquipButton.enabled &&
    panels.OverlayAICollectionUnequipButton.enabled,
    "Los botones no reflejan el estado equipado.");
panels.OverlayAICollectionUnequipButton.events.onactivate();
check(latestOutbound().message_type === "inventory.unequip",
    "No se envio inventory.unequip.");
check(Object.keys(latestOutbound().payload).length === 0,
    "inventory.unequip debe usar un payload vacio.");

apply(makeSnapshot(0, 0, 42));
apply(makeSnapshot(0, 0, 42));
check(panels.OverlayAICollectionList.children.length === 3,
    "Dos snapshots crearon tarjetas duplicadas.");

const outboundBeforeRemove = outbound.length;
panels.OverlayAICollectionRemoveButton.events.onactivate();
check(outbound.length === outboundBeforeRemove &&
    panels.OverlayAICollectionRemoveLabel.text === "CONFIRMAR",
    "Eliminar debe requerir una confirmacion separada.");
panels.OverlayAICollectionRemoveButton.events.onactivate();
check(latestOutbound("inventory.remove").payload.local_id === 42,
    "La confirmacion elimino otro Local ID.");

const beforeReconnect = outbound.length;
api.SetBridgeReady(false);
check(panels.OverlayAICollectionBridgeState.text === "DISCONNECTED",
    "La desconexion no se reflejo en la interfaz.");
api.SetBridgeReady(true);
check(outbound.length === beforeReconnect + 1 &&
    outbound[outbound.length - 1].message_type === "client.hello",
    "La reconexion debe realizar un nuevo handshake.");
check(outbound[outbound.length - 1].payload.client_session_id === originalSessionId,
    "Una reconexion del mismo frontend debe conservar su sesion.");

panels.OverlayAICollectionCloseButton.events.onactivate();
check(root.deleted, "Cerrar no elimino el root.");
check(!root.classes.has("Mounted"), "Cerrar dejo el panel montado.");
check(warnings.length === 0, "El frontend genero advertencias inesperadas.");

console.log("Panorama Phase 5 simulation: OK");
