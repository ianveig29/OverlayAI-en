"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

function check(condition, message) {
    if (!condition) throw new Error(message);
}

class MockPanel {
    constructor(type, parent, id) {
        this.type = type;
        this.parent = parent || null;
        this.id = id || "";
        this.children = [];
        this.style = {};
        this.attributes = new Map();
        this.events = new Map();
        this.enabled = true;
        this.text = "";
        this.image = "";
        this.actuallayoutwidth = 1920;
        this.actuallayoutheight = 1080;
        if (this.parent) this.parent.children.push(this);
    }

    FindChildTraverse(id) {
        for (const child of this.children) {
            if (child.id === id) return child;
            const nested = child.FindChildTraverse(id);
            if (nested) return nested;
        }
        return null;
    }

    RemoveAndDeleteChildren() {
        this.children = [];
    }

    SetAttributeString(name, value) {
        this.attributes.set(name, String(value));
    }

    GetAttributeString(name, fallback) {
        return this.attributes.has(name) ? this.attributes.get(name) : fallback;
    }

    SetPanelEvent(name, callback) {
        this.events.set(name, callback);
    }

    SetImage(value) {
        this.image = String(value);
    }
}

function extractScript(source, name) {
    const marker = `constexpr char ${name}[] = R"JS(`;
    const start = source.indexOf(marker);
    check(start >= 0, `No se encontro ${name}.`);
    const bodyStart = start + marker.length;
    const end = source.indexOf(")JS\";", bodyStart);
    check(end >= 0, `No se encontro el cierre de ${name}.`);
    return source.slice(bodyStart, end);
}

function formatScript(source, item, equipped) {
    let formatted = source.replace("%s", JSON.stringify(item));
    formatted = formatted.replace("%s", equipped ? "true" : "false");
    return formatted.replace(/%%/g, "%");
}

function runScript(source, root) {
    const dollar = {
        GetContextPanel: () => root,
        CreatePanel: (type, parent, id) => new MockPanel(type, parent, id),
        Schedule: (_delay, callback) => callback()
    };
    vm.runInNewContext(source, { $: dollar, Math, Number, String });
}

function onlyMount(root) {
    const mounts = root.children.filter(
        panel => panel.id === "OverlayAILocalInventoryRoot");
    check(mounts.length === 1, "Panorama debe conservar un unico root montado.");
    return mounts[0];
}

const bridgePath = path.resolve(
    __dirname, "..", "..", "OverlayAI.InventoryBridge", "PanoramaMount.cpp");
const source = fs.readFileSync(bridgePath, "utf8");
const revealTemplate = extractScript(source, "kRevealScriptFormat");
const collectionTemplate = extractScript(source, "kCollectionScriptFormat");
const nativeRevealObserver = extractScript(
    source, "kNativeRevealObserverScript");
check(nativeRevealObserver.includes("'UIPopupButtonClicked'"),
    "El observador nativo debe detectar la confirmacion del popup.");
check(nativeRevealObserver.includes("'close:' + pendingLocalId") &&
    nativeRevealObserver.includes("'overlayai_action'"),
    "La confirmacion nativa debe publicarse por el canal de acciones.");
const item = {
    local_id: 41,
    display_name: "Music Kit | Test",
    custom_name: "",
    image_url: "https://example.invalid/music.png",
    rarity: "High Grade",
    rarity_color: 4942335
};

const root = new MockPanel("Panel", null, "MainMenuRoot");
runScript(formatScript(revealTemplate, item, false), root);
let mount = onlyMount(root);
check(mount.GetAttributeString("overlayai_view", "") === "reveal",
    "La primera vista debe ser el reveal.");
check(mount.FindChildTraverse("OverlayAIRevealImage").image === item.image_url,
    "El reveal debe usar la imagen recibida.");

runScript(formatScript(revealTemplate, item, true), root);
mount = onlyMount(root);
check(mount.FindChildTraverse("OverlayAIRevealEquip").enabled === false,
    "Un articulo equipado debe bloquear EQUIPAR.");
mount.FindChildTraverse("OverlayAIRevealContinue").events.get("onactivate")();
check(root.GetAttributeString("overlayai_action", "") === "close:41",
    "CONTINUAR debe publicar close en el canal de acciones.");
check(root.GetAttributeString("overlayai_cmd", "") === "",
    "CONTINUAR no debe ocupar el canal reservado para diagnosticos.");

runScript(formatScript(collectionTemplate, item, true), root);
mount = onlyMount(root);
check(mount.GetAttributeString("overlayai_view", "") === "collection",
    "CONTINUAR debe poder reemplazar el reveal por la coleccion.");
check(mount.FindChildTraverse("OverlayAICollectionName").text === item.display_name,
    "La coleccion debe conservar el articulo confirmado.");
mount.FindChildTraverse("OverlayAICollectionClose").events.get("onactivate")();
check(root.GetAttributeString("overlayai_action", "") === "dismiss:41",
    "CERRAR debe solicitar el desmontaje por el canal de acciones.");

const smallRoot = new MockPanel("Panel", null, "SmallMainMenuRoot");
smallRoot.actuallayoutwidth = 1024;
smallRoot.actuallayoutheight = 768;
runScript(formatScript(revealTemplate, item, false), smallRoot);
const smallWidth = Number.parseInt(onlyMount(smallRoot).style.width, 10);
check(smallWidth < 760 && smallWidth <= 1024,
    "El reveal debe reducir su tamano en una resolucion menor.");

console.log("Panorama mount scripts: OK");
