param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$layout = Join-Path $ProjectRoot 'panorama\layout\overlayai_local_inventory.xml'
$style = Join-Path $ProjectRoot 'panorama\styles\overlayai_local_inventory.css'
$script = Join-Path $ProjectRoot 'panorama\scripts\overlayai_local_inventory.js'
$contract = Join-Path $ProjectRoot 'src\PanoramaBridgeContract.h'
$files = @($layout, $style, $script, $contract)

foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Falta el recurso Panorama: $file"
    }
}

$layoutText = Get-Content -LiteralPath $layout -Raw
$scriptText = Get-Content -LiteralPath $script -Raw
$contractText = Get-Content -LiteralPath $contract -Raw
[void][xml]$layoutText

if ([regex]::Matches($layoutText, 'id="OverlayAILocalInventoryRoot"').Count -ne 1) {
    throw 'El layout debe contener exactamente un root OverlayAILocalInventoryRoot.'
}

$requiredPanelIds = @(
    'OverlayAIRevealEquipButton',
    'OverlayAIContinueButton',
    'OverlayAICollectionList',
    'OverlayAICollectionEquipButton',
    'OverlayAICollectionUnequipButton',
    'OverlayAICollectionDuplicateButton',
    'OverlayAICollectionRemoveButton',
    'OverlayAICollectionSearch',
    'OverlayAICollectionRefreshButton',
    'OverlayAICollectionCloseButton'
)
foreach ($panelId in $requiredPanelIds) {
    if ([regex]::Matches($layoutText, 'id="' + [regex]::Escape($panelId) + '"').Count -ne 1) {
        throw "El layout debe contener exactamente un panel $panelId."
    }
}

$requiredScriptTokens = @(
    'GetFrontendAbiVersion',
    'SetBridgeReady',
    'ApplyMessage',
    'RequestRefresh',
    'api.Mount',
    'api.Unmount',
    'api.Destroy',
    'inventory.request_refresh',
    'inventory.equip',
    'inventory.unequip',
    'inventory.duplicate',
    'inventory.remove',
    'inventory.close_reveal'
)
foreach ($token in $requiredScriptTokens) {
    if (-not $scriptText.Contains($token)) {
        throw "Falta la entrada frontend requerida: $token"
    }
}

$forbiddenTokens = @(
    'InventoryAPI.',
    'SOCache',
    'SteamAPI',
    'WriteProcessMemory',
    'CreateRemoteThread',
    'CreatePanoramaUIEngineInternal',
    'eval('
)
foreach ($token in $forbiddenTokens) {
    if ($scriptText.Contains($token) -or $layoutText.Contains($token) -or $contractText.Contains($token)) {
        throw "El prototipo contiene una dependencia prohibida: $token"
    }
}

Write-Host 'Panorama prototype: OK'
Write-Host 'Root unico: OverlayAILocalInventoryRoot'
Write-Host 'Frontend ABI: 2 | Inventory protocol: 1'
