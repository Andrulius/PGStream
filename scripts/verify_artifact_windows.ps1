$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $Root
$Failures = New-Object System.Collections.Generic.List[string]

function Repair-ProcessPathEnvironment {
    $pathValue = [Environment]::GetEnvironmentVariable("Path", "Process")
    if (-not $pathValue) {
        $pathValue = [Environment]::GetEnvironmentVariable("PATH", "Process")
    }
    [Environment]::SetEnvironmentVariable("PATH", $null, "Process")
    if ($pathValue) {
        [Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")
    }
}

Repair-ProcessPathEnvironment

function Add-Failure([string] $Message) {
    $Failures.Add($Message) | Out-Null
    Write-Host "FAIL: $Message"
}

function Add-Pass([string] $Message) {
    Write-Host "PASS: $Message"
}

function Test-PeX64([string] $Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        return @{ Ok = $false; Machine = "not-MZ" }
    }

    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($peOffset -lt 0 -or $peOffset + 6 -ge $bytes.Length) {
        return @{ Ok = $false; Machine = "bad-PE-offset" }
    }

    if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45) {
        return @{ Ok = $false; Machine = "missing-PE" }
    }

    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    return @{ Ok = ($machine -eq 0x8664); Machine = ("0x{0:x4}" -f $machine) }
}

function Find-Dumpbin {
    $paths = @()
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $instances = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
        foreach ($instance in $instances) {
            $paths += Get-ChildItem -Path (Join-Path $instance.installationPath "VC\Tools\MSVC") -Filter dumpbin.exe -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -like "*Hostx64*x64*" } |
                Select-Object -ExpandProperty FullName
        }
    }

    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) {
        $paths += $command.Source
    }

    return $paths | Select-Object -First 1
}

function Show-Tree([string] $Path) {
    Get-ChildItem -LiteralPath $Path -Recurse | ForEach-Object {
        $relative = $_.FullName.Substring($Path.Length).TrimStart("\")
        if ($_.PSIsContainer) {
            Write-Host "  [D] $relative"
        } else {
            Write-Host "  [F] $relative"
        }
    }
}

$bundles = @(
    (Join-Path $Root "dist\PGStream.vst3"),
    (Join-Path $Root "PGStream.vst3")
)

foreach ($bundle in $bundles) {
    Write-Host ""
    Write-Host "Checking bundle: $bundle"

    if (-not (Test-Path -LiteralPath $bundle -PathType Container)) {
        Add-Failure "$bundle does not exist as a folder bundle."
        continue
    }
    Add-Pass "$bundle exists as a folder bundle."

    $moduleInfo = Join-Path $bundle "Contents\Resources\moduleinfo.json"
    $innerBinary = Join-Path $bundle "Contents\x86_64-win\PGStream.vst3"

    if (Test-Path -LiteralPath $moduleInfo -PathType Leaf) {
        Add-Pass "moduleinfo.json exists at Contents\Resources."
        try {
            $json = Get-Content -LiteralPath $moduleInfo -Raw | ConvertFrom-Json
            $jsonText = Get-Content -LiteralPath $moduleInfo -Raw
            if ($jsonText -match "PGStream") {
                Add-Pass "moduleinfo.json references PGStream."
            } else {
                Add-Failure "moduleinfo.json does not reference PGStream."
            }
        } catch {
            Add-Failure "moduleinfo.json is not valid JSON: $($_.Exception.Message)"
        }
    } else {
        Add-Failure "moduleinfo.json missing at Contents\Resources."
    }

    if (Test-Path -LiteralPath $innerBinary -PathType Leaf) {
        Add-Pass "inner binary exists at Contents\x86_64-win\PGStream.vst3."
        $pe = Test-PeX64 $innerBinary
        if ($pe.Ok) {
            Add-Pass "inner binary is Windows x64 PE ($($pe.Machine))."
        } else {
            Add-Failure "inner binary is not Windows x64 PE ($($pe.Machine))."
        }
    } else {
        Add-Failure "inner binary missing at Contents\x86_64-win\PGStream.vst3."
    }

    $outerName = Split-Path $bundle -Leaf
    $innerName = Split-Path $innerBinary -Leaf
    if ($outerName -eq "PGStream.vst3" -and $innerName -eq "PGStream.vst3") {
        Add-Pass "outer bundle name and inner binary name are consistent."
    } else {
        Add-Failure "bundle naming is inconsistent: outer=$outerName inner=$innerName"
    }

    Write-Host "Bundle tree:"
    Show-Tree $bundle
}

$distInner = Join-Path $Root "dist\PGStream.vst3\Contents\x86_64-win\PGStream.vst3"
$dumpbin = Find-Dumpbin
if ($dumpbin -and (Test-Path -LiteralPath $distInner)) {
    Write-Host ""
    Write-Host "dumpbin: $dumpbin"
    Write-Host "Inner binary path: $distInner"
    & $dumpbin /HEADERS $distInner | Select-String -Pattern "machine|x64|DLL" -SimpleMatch
    $dependencies = & $dumpbin /DEPENDENTS $distInner
    $dependencies | ForEach-Object { Write-Host $_ }

    if ($dependencies -match "libssl|libcrypto") {
        Add-Failure "OpenSSL DLL dependency detected; expected static OpenSSL linkage."
    } else {
        Add-Pass "No libssl/libcrypto DLL dependency detected."
    }
} else {
    Write-Host "dumpbin was not found; PE header parser was used for architecture checks."
}

Write-Host ""
if ($Failures.Count -gt 0) {
    Write-Host "VERIFY RESULT: FAIL"
    foreach ($failure in $Failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "VERIFY RESULT: PASS"
exit 0
