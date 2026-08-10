param(
    [string]$SessionId = "",
    [switch]$ExerciseActions,
    [switch]$ExerciseCollectionActions
)

$ErrorActionPreference = "Stop"
$pipeName = "OverlayAI.Inventory.v1"
$utf8 = [System.Text.UTF8Encoding]::new($false)
$script:requestId = [uint64]1

if ([string]::IsNullOrWhiteSpace($SessionId)) {
    $SessionId = "overlayai-ipc-$PID-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
}

function Connect-InventoryPipe {
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        ".", $pipeName,
        [System.IO.Pipes.PipeDirection]::InOut,
        [System.IO.Pipes.PipeOptions]::None)
    $pipe.Connect(3000)
    return $pipe
}

function Write-ExactJson([System.IO.Stream]$Pipe, [hashtable]$Message) {
    $json = $Message | ConvertTo-Json -Depth 20 -Compress
    $payload = $utf8.GetBytes($json)
    if ($payload.Length -gt 262144) { throw "Frame demasiado grande" }
    $length = [System.BitConverter]::GetBytes([uint32]$payload.Length)
    $Pipe.Write($length, 0, $length.Length)
    $Pipe.Write($payload, 0, $payload.Length)
    $Pipe.Flush()
}

function Read-Exact([System.IO.Stream]$Pipe, [int]$Count) {
    $buffer = [byte[]]::new($Count)
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Pipe.Read($buffer, $offset, $Count - $offset)
        if ($read -le 0) { throw "El servidor cerro la conexion" }
        $offset += $read
    }
    return $buffer
}

function Read-InventoryResponse([System.IO.Stream]$Pipe) {
    $lengthBytes = Read-Exact $Pipe 4
    $length = [System.BitConverter]::ToUInt32($lengthBytes, 0)
    if ($length -eq 0 -or $length -gt 262144) { throw "Longitud de respuesta invalida" }
    $payload = Read-Exact $Pipe ([int]$length)
    return ($utf8.GetString($payload) | ConvertFrom-Json)
}

function Invoke-Request([System.IO.Stream]$Pipe, [hashtable]$Message) {
    Write-ExactJson $Pipe $Message
    return Read-InventoryResponse $Pipe
}

function New-Request([string]$Type, [hashtable]$Payload) {
    $message = @{
        protocol_version = 1
        message_type = $Type
        request_id = $script:requestId
        payload = $Payload
    }
    $script:requestId++
    return $message
}

function Assert-Snapshot($Response, [string]$Context) {
    if ($null -eq $Response -or $Response.message_type -ne "inventory.snapshot") {
        throw "$Context no devolvio inventory.snapshot"
    }
    $items = @($Response.payload.items)
    $ids = @($items | ForEach-Object { [uint64]$_.local_id })
    if (@($ids | Sort-Object -Unique).Count -ne $ids.Count) {
        throw "$Context devolvio Local IDs duplicados"
    }
}

function Assert-Action($Response, [string]$ExpectedType, [string]$Context) {
    if ($null -eq $Response -or $Response.message_type -ne $ExpectedType -or
        $Response.payload.success -ne $true) {
        $detail = if ($null -ne $Response) { $Response.payload.detail } else { "sin respuesta" }
        throw "$Context fallo: $detail"
    }
}

$hello = New-Request "client.hello" @{ client_session_id = $SessionId }
$refresh = New-Request "inventory.request_refresh" @{}
$pipe = Connect-InventoryPipe
try {
    $helloResponse = Invoke-Request $pipe $hello
    Assert-Snapshot $helloResponse "Handshake"
    $refreshResponse = Invoke-Request $pipe $refresh
    Assert-Snapshot $refreshResponse "Refresh"
} finally {
    $pipe.Dispose()
}

$pipe = Connect-InventoryPipe
try {
    $reconnectHello = New-Request "client.hello" @{ client_session_id = $SessionId }
    $reconnectResponse = Invoke-Request $pipe $reconnectHello
    Assert-Snapshot $reconnectResponse "Reconexion"

    $duplicateResponse = Invoke-Request $pipe $refresh
    Assert-Snapshot $duplicateResponse "Request idempotente"
    $snapshot = $duplicateResponse

    if ($ExerciseActions) {
        $musicKits = @($snapshot.payload.items | Where-Object {
            [int]$_.type -eq 0 -and [string]$_.validity -eq "Valido"
        })
        if ($musicKits.Count -eq 0) {
            throw "No hay Music Kits validos para comprobar la Fase 4"
        }

        $originalEquipped = [uint64]$snapshot.payload.loadout.music_kit
        $targetId = [uint64]$musicKits[0].local_id
        $pendingId = [uint64]$snapshot.payload.pending_reveal_item_id

        if ($pendingId -ne 0) {
            $closed = Invoke-Request $pipe (New-Request "inventory.close_reveal" @{
                local_id = $pendingId
            })
            Assert-Action $closed "inventory.reveal_closed" "Cerrar reveal"
        }

        $equipped = Invoke-Request $pipe (New-Request "inventory.equip" @{
            local_id = $targetId
        })
        Assert-Action $equipped "inventory.loadout_changed" "Equipar"
        $snapshot = Invoke-Request $pipe (New-Request "inventory.request_refresh" @{})
        Assert-Snapshot $snapshot "Snapshot equipado"
        if ([uint64]$snapshot.payload.loadout.music_kit -ne $targetId) {
            throw "El snapshot no refleja el Music Kit equipado"
        }

        $unequipped = Invoke-Request $pipe (New-Request "inventory.unequip" @{})
        Assert-Action $unequipped "inventory.loadout_changed" "Desequipar"
        $snapshot = Invoke-Request $pipe (New-Request "inventory.request_refresh" @{})
        Assert-Snapshot $snapshot "Snapshot desequipado"
        if ([uint64]$snapshot.payload.loadout.music_kit -ne 0) {
            throw "El snapshot conserva un Music Kit despues de desequipar"
        }

        if ($originalEquipped -ne 0) {
            $restored = Invoke-Request $pipe (New-Request "inventory.equip" @{
                local_id = $originalEquipped
            })
            Assert-Action $restored "inventory.loadout_changed" "Restaurar loadout"
        }
    }

    if ($ExerciseCollectionActions) {
        $validItems = @($snapshot.payload.items | Where-Object {
            [string]$_.validity -eq "Valido"
        })
        if ($validItems.Count -eq 0) {
            throw "No hay articulos validos para comprobar duplicado y eliminacion"
        }

        $baselineCount = @($snapshot.payload.items).Count
        $sourceId = [uint64]$validItems[0].local_id
        $duplicateRequest = New-Request "inventory.duplicate" @{ local_id = $sourceId }
        $duplicated = Invoke-Request $pipe $duplicateRequest
        Assert-Action $duplicated "inventory.item_added" "Duplicar instancia"
        $duplicatedId = [uint64]$duplicated.payload.local_id
        if ($duplicatedId -eq 0 -or $duplicatedId -eq $sourceId) {
            throw "Duplicar no asigno un Local ID independiente"
        }

        $snapshot = Invoke-Request $pipe (New-Request "inventory.request_refresh" @{})
        Assert-Snapshot $snapshot "Snapshot duplicado"
        if (@($snapshot.payload.items).Count -ne $baselineCount + 1) {
            throw "La coleccion no contiene exactamente una copia nueva"
        }

        $idempotent = Invoke-Request $pipe $duplicateRequest
        Assert-Snapshot $idempotent "Duplicado idempotente"
        if (@($idempotent.payload.items).Count -ne $baselineCount + 1) {
            throw "Repetir request_id creo una segunda copia"
        }

        $removed = Invoke-Request $pipe (New-Request "inventory.remove" @{
            local_id = $duplicatedId
        })
        Assert-Action $removed "inventory.item_removed" "Eliminar copia"
        $snapshot = Invoke-Request $pipe (New-Request "inventory.request_refresh" @{})
        Assert-Snapshot $snapshot "Snapshot restaurado"
        if (@($snapshot.payload.items).Count -ne $baselineCount) {
            throw "Eliminar la copia no restauro el tamano original"
        }
    }

    $mode = if ($ExerciseCollectionActions) { "PHASE5" }
        elseif ($ExerciseActions) { "PHASE4" }
        else { "READ_ONLY" }
    Write-Output ("IPC $mode OK | Items: " + @($snapshot.payload.items).Count +
        " | Pending reveal: " + $snapshot.payload.pending_reveal_item_id +
        " | Session: " + $SessionId)
} finally {
    $pipe.Dispose()
}
