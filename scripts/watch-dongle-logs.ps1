param(
    [string]$LogPath = "$env:USERPROFILE\Downloads\charybdis_dongle.log",
    [string[]]$PortName,
    [int]$BaudRate = 115200
)

$ErrorActionPreference = "Stop"

# Kill any other running instance of this script before starting
Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in 'powershell.exe', 'pwsh.exe' -and
    $_.CommandLine -like '*watch-dongle-logs*' -and
    $_.ProcessId -ne $PID
} | ForEach-Object {
    Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
    Write-Host "Killed previous instance (PID $($_.ProcessId))"
}

function Get-CandidatePortScore {
    param(
        [Parameter(Mandatory = $true)]
        $Port
    )

    $score = 0
    $pnpId = [string]$Port.PNPDeviceID
    $name = [string]$Port.Name

    if ($pnpId -like "USB\VID_0011&PID_0007&MI_03*") { $score += 500 }
    elseif ($pnpId -like "USB\VID_1D50&PID_615E&MI_03*") { $score += 450 }
    elseif ($pnpId -like "USB\VID_239A&*MI_03*") { $score += 400 }
    elseif ($pnpId -like "USB\VID_2886&*MI_03*") { $score += 375 }
    elseif ($pnpId -like "USB\VID_0011&PID_0007*") { $score += 350 }
    elseif ($pnpId -like "USB\VID_1D50&PID_615E*") { $score += 300 }
    elseif ($pnpId -like "USB\VID_239A*") { $score += 250 }
    elseif ($pnpId -like "USB\VID_2886*") { $score += 225 }

    if ($pnpId -match "&MI_03\\") { $score += 100 }
    elseif ($pnpId -match "&MI_") { $score += 25 }

    if ($name -like "USB Serial Device*") { $score += 40 }

    return $score
}

function Test-LikelyZmkUsbDevice {
    param($Device)

    $pnpId = [string]$Device.PNPDeviceID
    $name = [string]$Device.Name

    return (
        $pnpId -like "USB\VID_0011&PID_0007*" -or
        $pnpId -like "USB\VID_1D50&PID_615E*" -or
        $pnpId -like "USB\VID_239A*" -or
        $pnpId -like "USB\VID_2886*" -or
        $name -like "*ZMK*" -or
        $name -like "*roBa*" -or
        $name -like "*XIAO*" -or
        $name -like "*CDC*"
    )
}

function Get-DongleSerialPorts {
    $ports = @(Get-CimInstance Win32_SerialPort | Where-Object {
            Test-LikelyZmkUsbDevice $_
        })

    return $ports |
        Sort-Object @{ Expression = { Get-CandidatePortScore $_ }; Descending = $true }, DeviceID
}

function Test-DongleUsbInterfacePresent {
    $interfaces = @(Get-CimInstance Win32_PnPEntity | Where-Object {
            Test-LikelyZmkUsbDevice $_
        })

    return $interfaces.Count -gt 0
}

function Show-DriverHint {
    Write-Warning "A likely ZMK USB device is present, but Windows has not exposed a COM port for it."
    Write-Warning "Open Device Manager, find the USB device/interface, then Update driver -> Browse my computer -> Let me pick -> Ports (COM & LPT) -> USB Serial Device."
    Write-Warning "If that succeeds, replug the dongle and rerun this script."
}

# Per-port monitor loop — runs in its own runspace
$portScript = {
    param($portName, $baudRate, $logPath)

    while ($true) {
        $port = $null
        try {
            $port = New-Object System.IO.Ports.SerialPort $portName, $baudRate, "None", 8, "One"
            $port.ReadTimeout = 500
            $port.DtrEnable = $true
            $port.RtsEnable = $false
            $port.Open()
            $port.DiscardInBuffer()
            Start-Sleep -Milliseconds 200
            [Console]::WriteLine("--- connected $portName ---")

            $lineBuffer = ""
            while ($true) {
                $data = $port.ReadExisting()
                if ($data.Length -gt 0) {
                    $lineBuffer += $data
                    $lines = $lineBuffer -split "`n"
                    for ($i = 0; $i -lt $lines.Count - 1; $i++) {
                        $line = "[$portName] " + $lines[$i].TrimEnd("`r")
                        [Console]::WriteLine($line)
                        [System.IO.File]::AppendAllText($logPath, $line + "`n")
                    }
                    $lineBuffer = $lines[-1]
                } else {
                    Start-Sleep -Milliseconds 50
                }
            }
        } catch {
            # port open failed, read error, or write error — fall through to reconnect
        } finally {
            if ($port -ne $null) {
                try { $port.Close() } catch {}
                try { $port.Dispose() } catch {}
            }
        }

        # Check if the COM port still exists in Windows before retrying.
        # If the device was unplugged, exit the runspace instead of looping forever.
        $portExists = [bool](Get-CimInstance Win32_SerialPort -Filter "DeviceID='$portName'" -ErrorAction SilentlyContinue)
        if (-not $portExists) {
            [Console]::WriteLine("--- $portName gone, stopping monitor ---")
            return
        }

        [Console]::WriteLine("--- $portName disconnected, retrying ---")
        Start-Sleep -Seconds 1
    }
}

function Start-PortMonitor {
    param([string]$portName, [int]$baudRate, [string]$logPath)

    $rs = [runspacefactory]::CreateRunspace()
    $rs.Open()
    $ps = [powershell]::Create()
    $ps.Runspace = $rs
    $ps.AddScript($portScript).AddArgument($portName).AddArgument($baudRate).AddArgument($logPath) | Out-Null
    [void]$ps.BeginInvoke()
}

Write-Host "Logging to $LogPath"
Write-Host "Watching ZMK dongle logs. Ctrl-C to stop."

# ── startup diagnostic ──────────────────────────────────────────────────────
Write-Host ""
Write-Host "=== All COM ports on this machine ==="
Get-CimInstance Win32_SerialPort | Sort-Object DeviceID |
    Select-Object DeviceID, Name, @{N="PNP";E={$_.PNPDeviceID}} |
    Format-Table -AutoSize

Write-Host "=== All USB PnP entities (VID_1D50, VID_1209, VID_239A, VID_2886, VID_0011) ==="
Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -match "USB\\VID_(1D50|1209|239A|2886|0011)"
} | Sort-Object PNPDeviceID |
    Select-Object @{N="PNP";E={$_.PNPDeviceID}}, Name, Status |
    Format-Table -AutoSize
Write-Host "──────────────────────────────────────────────────────────────────"
Write-Host ""
# ────────────────────────────────────────────────────────────────────────────

# -PortName forces specific ports; otherwise auto-detect all matching ports
if ($PortName -and $PortName.Count -gt 0) {
    $activePorts = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($p in $PortName) {
        Write-Host "Monitoring $p (manual)"
        Start-PortMonitor -portName $p -baudRate $BaudRate -logPath $LogPath
        $activePorts.Add($p) | Out-Null
    }
    # Keep main thread alive
    while ($true) { Start-Sleep -Seconds 60 }
} else {
    $activePorts = [System.Collections.Generic.HashSet[string]]::new()
    $driverHintShown = $false

    while ($true) {
        $found = @(Get-DongleSerialPorts)

        if ($found.Count -eq 0) {
            if (-not $driverHintShown -and (Test-DongleUsbInterfacePresent)) {
                Show-DriverHint
                $driverHintShown = $true
            }
        } else {
            $driverHintShown = $false
            foreach ($p in $found) {
                if ($activePorts.Add($p.DeviceID)) {
                    Write-Host "Found $($p.DeviceID) -> starting monitor"
                    Start-PortMonitor -portName $p.DeviceID -baudRate $BaudRate -logPath $LogPath
                }
            }
        }

        Start-Sleep -Seconds 2
    }
}
