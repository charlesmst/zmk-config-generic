param(
    [string]$LogPath = "$env:USERPROFILE\Downloads\charybdis_dongle.log",
    [string]$PortName,
    [int]$BaudRate = 115200
)

$ErrorActionPreference = "Stop"

function Get-CandidatePortScore {
    param(
        [Parameter(Mandatory = $true)]
        $Port
    )

    $score = 0
    $pnpId = [string]$Port.PNPDeviceID
    $name = [string]$Port.Name

    if ($pnpId -like "USB\VID_0011&PID_0007&MI_03*") { $score += 500 }
    elseif ($pnpId -like "USB\VID_1209&PID_C000&MI_03*") { $score += 450 }
    elseif ($pnpId -like "USB\VID_1D50&PID_615E&MI_03*") { $score += 425 }
    elseif ($pnpId -like "USB\VID_0011&PID_0007*") { $score += 350 }
    elseif ($pnpId -like "USB\VID_1209&PID_C000*") { $score += 300 }
    elseif ($pnpId -like "USB\VID_1D50&PID_615E*") { $score += 275 }

    if ($pnpId -match "&MI_03\\") { $score += 100 }
    elseif ($pnpId -match "&MI_") { $score += 25 }

    if ($name -like "USB Serial Device*") { $score += 40 }

    return $score
}

function Get-DongleSerialPorts {
    $ports = @(Get-CimInstance Win32_SerialPort | Where-Object {
            $_.PNPDeviceID -like "USB\VID_0011&PID_0007*" -or
            $_.PNPDeviceID -like "USB\VID_1209&PID_C000*" -or
            $_.PNPDeviceID -like "USB\VID_1D50&PID_615E*"
        })

    return $ports |
        Sort-Object @{ Expression = { Get-CandidatePortScore $_ }; Descending = $true }, DeviceID
}

function Test-DongleUsbInterfacePresent {
    $interfaces = @(Get-CimInstance Win32_PnPEntity | Where-Object {
            $_.PNPDeviceID -like "USB\VID_0011&PID_0007*" -or
            $_.PNPDeviceID -like "USB\VID_1209&PID_C000*" -or
            $_.PNPDeviceID -like "USB\VID_1D50&PID_615E*"
        })

    return $interfaces.Count -gt 0
}

function Show-DriverHint {
    Write-Warning "A likely ZMK USB device is present, but Windows has not exposed a COM port for it."
    Write-Warning "Open Device Manager, find the USB device/interface, then Update driver -> Browse my computer -> Let me pick -> Ports (COM & LPT) -> USB Serial Device."
    Write-Warning "If that succeeds, replug the dongle and rerun this script."
}

function Get-DonglePortName {
    if ($PortName) {
        return $PortName
    }

    $ports = @(Get-DongleSerialPorts)
    if ($ports.Count -gt 0) {
        return $ports[0].DeviceID
    }

    return $null
}

Write-Host "Logging to $LogPath"
Write-Host "Watching ZMK dongle logs. Ctrl-C to stop."
Write-Host "Auto-detect prefers 0011:0007 MI_03, then 1209:C000 MI_03, then 1D50:615E MI_03."

$driverHintShown = $false

while ($true) {
    $currentPortName = Get-DonglePortName
    if (-not $currentPortName) {
        if (-not $driverHintShown -and (Test-DongleUsbInterfacePresent)) {
            Show-DriverHint
            $driverHintShown = $true
        }
        Start-Sleep -Seconds 1
        continue
    }

    $driverHintShown = $false
    $port = New-Object System.IO.Ports.SerialPort $currentPortName, $BaudRate, "None", 8, "One"
    $port.ReadTimeout = 500
    $port.DtrEnable = $true
    $port.RtsEnable = $false

    try {
        $port.Open()
        $port.DiscardInBuffer()
        Start-Sleep -Milliseconds 200
        Write-Host "--- connected $currentPortName (baud=$BaudRate dtr=$($port.DtrEnable) rts=$($port.RtsEnable)) ---"
        Write-Host "Note: USB CDC ACM ignores host baud on most ZMK builds; DTR is the important signal."

        while ($true) {
            try {
                $data = $port.ReadExisting()
                if ($data.Length -gt 0) {
                    Write-Host -NoNewline $data
                    Add-Content -Path $LogPath -Value $data
                } else {
                    Start-Sleep -Milliseconds 100
                }
            } catch {
                break
            }
        }
    } finally {
        if ($port -and $port.IsOpen) {
            $port.Close()
        }
    }

    Write-Host "--- disconnected, retrying ---"
    Start-Sleep -Seconds 1
}
