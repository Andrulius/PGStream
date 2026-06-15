$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $Root

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

function Find-CMake {
    $local = Join-Path $Root "tools\cmake-4.3.3-windows-x86_64\bin\cmake.exe"
    if (Test-Path -LiteralPath $local) {
        return $local
    }
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "CMake was not found. Run scripts\bootstrap_windows.ps1 first."
}

function Find-VsWhere {
    $candidate = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $candidate) {
        return $candidate
    }
    $command = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "vswhere was not found."
}

function Select-Preset {
    $vswhere = Find-VsWhere
    $instances = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
    $vs2022 = $instances | Where-Object { $_.installationVersion -like "17.*" } | Select-Object -First 1
    if ($vs2022) {
        return @{ Configure = "vs2022-x64-safe"; Build = "vs2022-safe-release"; Instance = $vs2022.displayName }
    }

    $vs2019 = $instances | Where-Object { $_.installationVersion -like "16.*" } | Select-Object -First 1
    if ($vs2019) {
        return @{ Configure = "vs2019-x64-safe"; Build = "vs2019-safe-release"; Instance = $vs2019.displayName }
    }

    throw "No VS 2022 or VS 2019 C++ toolchain was found."
}

function Assert-ProjectPath([string] $Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($Root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify path outside project: $full"
    }
    return $full
}

function Copy-Bundle([string] $Source, [string] $Destination) {
    $destinationFull = Assert-ProjectPath $Destination
    if (Test-Path -LiteralPath $destinationFull) {
        Remove-Item -LiteralPath $destinationFull -Recurse -Force
    }
    Copy-Item -LiteralPath $Source -Destination $destinationFull -Recurse -Force
}

function Repair-ModuleInfoJson([string] $BundlePath) {
    $moduleInfo = Join-Path $BundlePath "Contents\Resources\moduleinfo.json"
    if (-not (Test-Path -LiteralPath $moduleInfo)) {
        throw "moduleinfo.json missing after copy: $moduleInfo"
    }

    $text = Get-Content -LiteralPath $moduleInfo -Raw
    $fixed = [regex]::Replace($text, ",(\s*[}\]])", '$1')
    Set-Content -LiteralPath $moduleInfo -Value $fixed -Encoding ASCII
    $null = Get-Content -LiteralPath $moduleInfo -Raw | ConvertFrom-Json
}

Repair-ProcessPathEnvironment
& (Join-Path $PSScriptRoot "generate_dev_cert.ps1") -Root $Root

$cmake = Find-CMake
$preset = Select-Preset

Write-Host "Using CMake: $cmake"
Write-Host "Using generator preset: $($preset.Configure) via $($preset.Instance)"
Write-Host "Command: $cmake --preset $($preset.Configure)"
& $cmake --preset $preset.Configure
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

Write-Host "Command: $cmake --build --preset $($preset.Build) --parallel 1"
& $cmake --build --preset $preset.Build --parallel 1
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed."
}

$bundle = Get-ChildItem -Path (Join-Path $Root "build") -Directory -Recurse -Filter "PGStream.vst3" |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "Contents\x86_64-win\PGStream.vst3") } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $bundle) {
    throw "Could not locate a complete PGStream.vst3 bundle under build."
}

New-Item -ItemType Directory -Force -Path (Join-Path $Root "dist") | Out-Null

$distBundle = Join-Path $Root "dist\PGStream.vst3"
$rootBundle = Join-Path $Root "PGStream.vst3"
Copy-Bundle $bundle.FullName $distBundle
Copy-Bundle $bundle.FullName $rootBundle
Repair-ModuleInfoJson $distBundle
Repair-ModuleInfoJson $rootBundle

Write-Host "Copied complete VST3 bundles:"
Write-Host "  $distBundle"
Write-Host "  $rootBundle"
