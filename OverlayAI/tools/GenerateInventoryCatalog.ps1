param(
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\src\InventoryCatalogData.inc')
)

$ErrorActionPreference = 'Stop'
$baseUrl = 'https://raw.githubusercontent.com/ByMykel/CSGO-API/main/public/api/en'
$cacheDirectory = Join-Path $env:TEMP 'overlayai_inventory_catalog'
New-Item -ItemType Directory -Force -Path $cacheDirectory | Out-Null

function Read-CatalogJson([string]$name) {
    $target = Join-Path $cacheDirectory $name
    try {
        Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/$name" -OutFile $target
    }
    catch {
        if (-not (Test-Path -LiteralPath $target)) { throw }
        Write-Warning "Could not refresh $name; using the cached catalog."
    }
    return Get-Content -Raw -Encoding UTF8 -LiteralPath $target | ConvertFrom-Json
}

function Convert-ToAscii([string]$value) {
    if ([string]::IsNullOrWhiteSpace($value)) { return '' }
    $value = $value.Replace([string][char]0x2122, ' TM').Replace([string][char]0x2605, '')
    $normalized = $value.Normalize([Text.NormalizationForm]::FormD)
    $builder = [Text.StringBuilder]::new()
    foreach ($character in $normalized.ToCharArray()) {
        $category = [Globalization.CharUnicodeInfo]::GetUnicodeCategory($character)
        if ($category -eq [Globalization.UnicodeCategory]::NonSpacingMark) { continue }
        $code = [int]$character
        if ($code -ge 32 -and $code -le 126) {
            [void]$builder.Append($character)
        }
    }
    return $builder.ToString().Trim().Replace('\', '\\').Replace('"', '\"')
}

function Convert-Color([string]$color) {
    if ($color -match '^#([0-9a-fA-F]{6})$') { return "0x$($Matches[1])" }
    return '0xB0B0B0'
}

function Convert-Float($value) {
    return ([double]$value).ToString('0.000000', [Globalization.CultureInfo]::InvariantCulture) + 'f'
}

$musicKits = Read-CatalogJson 'music_kits.json'
$agents = Read-CatalogJson 'agents.json'
$skins = Read-CatalogJson 'skins.json'
$collectibles = Read-CatalogJson 'collectibles.json'
$crates = Read-CatalogJson 'crates.json'
$keys = Read-CatalogJson 'keys.json'
$stickers = Read-CatalogJson 'stickers.json'
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('// Generated from ByMykel/CSGO-API. Run tools/GenerateInventoryCatalog.ps1 to refresh.')

$musicKits |
    Group-Object { [int]$_.def_index } |
    Sort-Object { [int]$_.Name } |
    ForEach-Object {
        $item = $_.Group | Where-Object { $_.id -notlike '*_st' } | Select-Object -First 1
        if (-not $item) { $item = $_.Group[0] }
        $name = Convert-ToAscii $item.name
        $rarity = Convert-ToAscii $item.rarity.name
        $color = Convert-Color $item.rarity.color
        $image = Convert-ToAscii $item.image
        $hasStatTrak = [int]$item.def_index -gt 2 -and
            ($_.Group | Where-Object { $_.id -like '*_st' } | Select-Object -First 1)
        $statTrak = if ($hasStatTrak) { 'true' } else { 'false' }
        $lines.Add("{ LocalInventoryMusicKit, $([int]$item.def_index), 0, 0.0f, 1.0f, $statTrak, false, 4, $color, `"$name`", `"Music Kits`", `"$rarity`", `"$image`" },")
    }

$agents |
    Sort-Object name |
    ForEach-Object {
        $name = Convert-ToAscii $_.name
        $group = Convert-ToAscii $_.team.name
        $rarity = Convert-ToAscii $_.rarity.name
        $color = Convert-Color $_.rarity.color
        $image = Convert-ToAscii $_.image
        $lines.Add("{ LocalInventoryAgent, $([int]$_.def_index), 0, 0.0f, 1.0f, false, false, 4, $color, `"$name`", `"$group`", `"$rarity`", `"$image`" },")
    }

$skins |
    Sort-Object name, { [int]$_.weapon.weapon_id } |
    ForEach-Object {
        $type = switch ($_.category.name) {
            'Knives' { 'LocalInventoryKnife' }
            'Gloves' { 'LocalInventoryGloves' }
            default { 'LocalInventoryWeaponSkin' }
        }
        $name = Convert-ToAscii $_.name
        $phase = Convert-ToAscii $_.phase
        if (-not [string]::IsNullOrWhiteSpace($phase)) {
            $name = "$name | $phase"
        }
        $group = Convert-ToAscii $_.weapon.name
        $rarity = Convert-ToAscii $_.rarity.name
        $color = Convert-Color $_.rarity.color
        $statTrak = if ($_.stattrak) { 'true' } else { 'false' }
        $souvenir = if ($_.souvenir) { 'true' } else { 'false' }
        $minWear = Convert-Float $_.min_float
        $maxWear = Convert-Float $_.max_float
        $image = Convert-ToAscii $_.image
        $legacyModel = if ($_.legacy_model) { 'true' } else { 'false' }
        $quality = if ($type -eq 'LocalInventoryKnife' -or $type -eq 'LocalInventoryGloves') { 3 } else { 4 }
        $lines.Add("{ $type, $([int]$_.weapon.weapon_id), $([int]$_.paint_index), $minWear, $maxWear, $statTrak, $souvenir, $quality, $color, `"$name`", `"$group`", `"$rarity`", `"$image`", $legacyModel },")
    }

$collectibles |
    Sort-Object { [int]$_.def_index } |
    ForEach-Object {
        $name = Convert-ToAscii $_.name
        $group = Convert-ToAscii $_.type
        if ([string]::IsNullOrWhiteSpace($group)) { $group = 'Collectible' }
        $rarity = Convert-ToAscii $_.rarity.name
        $color = Convert-Color $_.rarity.color
        $image = Convert-ToAscii $_.image
        $quality = if ($_.genuine) { 1 } else { 4 }
        $lines.Add("{ LocalInventoryCollectible, $([int]$_.def_index), 0, 0.0f, 1.0f, false, false, $quality, $color, `"$name`", `"$group`", `"$rarity`", `"$image`" },")
    }

$crates |
    Group-Object { [int]$_.def_index } |
    Sort-Object { [int]$_.Name } |
    ForEach-Object {
        # Highlight souvenir packages share a definition and need additional
        # match data. Keep the regular package until that metadata is modeled.
        $item = $_.Group |
            Where-Object { $_.type -ne 'Souvenir Highlight' } |
            Select-Object -First 1
        if (-not $item) { $item = $_.Group[0] }
        $name = Convert-ToAscii $item.name
        $group = Convert-ToAscii $item.type
        if ([string]::IsNullOrWhiteSpace($group)) { $group = 'Container' }
        $rarity = Convert-ToAscii $item.rarity.name
        if ([string]::IsNullOrWhiteSpace($rarity)) { $rarity = 'Base Grade' }
        $color = Convert-Color $item.rarity.color
        $image = Convert-ToAscii $item.image
        $lines.Add("{ LocalInventoryContainer, $([int]$item.def_index), 0, 0.0f, 1.0f, false, false, 4, $color, `"$name`", `"$group`", `"$rarity`", `"$image`" },")
    }

$keys |
    Sort-Object { [int]$_.def_index } |
    ForEach-Object {
        $name = Convert-ToAscii $_.name
        $image = Convert-ToAscii $_.image
        $lines.Add("{ LocalInventoryKey, $([int]$_.def_index), 0, 0.0f, 1.0f, false, false, 4, 0xB0C3D9, `"$name`", `"Keys`", `"Base Grade`", `"$image`" },")
    }

$stickers |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_.market_hash_name) } |
    Sort-Object { [int]$_.def_index } |
    ForEach-Object {
        $name = Convert-ToAscii $_.name
        $group = Convert-ToAscii $_.type
        if ([string]::IsNullOrWhiteSpace($group)) { $group = 'Sticker' }
        $rarity = Convert-ToAscii $_.rarity.name
        $color = Convert-Color $_.rarity.color
        $image = Convert-ToAscii $_.image
        # 1209 is the standalone sticker item. Attribute 113 receives the
        # sticker-kit id stored here in paintIndex for snapshot compatibility.
        $lines.Add("{ LocalInventorySticker, 1209, $([int]$_.def_index), 0.0f, 1.0f, false, false, 4, $color, `"$name`", `"$group`", `"$rarity`", `"$image`" },")
    }

$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($resolvedOutput)) | Out-Null
[IO.File]::WriteAllLines($resolvedOutput, $lines, [Text.UTF8Encoding]::new($false))
Write-Host "Generated $($lines.Count - 1) catalog entries at $resolvedOutput"
