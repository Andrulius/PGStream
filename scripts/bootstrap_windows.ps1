$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ToolsDir = Join-Path $Root "tools"
$CMakeVersion = "4.3.3"
$CMakeDir = Join-Path $ToolsDir "cmake-$CMakeVersion-windows-x86_64"
$CMakeExe = Join-Path $CMakeDir "bin\cmake.exe"

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

function Write-Section($name) {
    Write-Host ""
    Write-Host "== $name =="
}

function Find-RealGit {
    $candidates = @(
        "C:\Program Files\Git\cmd\git.exe",
        "C:\Program Files\Git\bin\git.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $command = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($command -and $command.Source -notlike "*\Windows\System32\git") {
        return $command.Source
    }

    throw "Git for Windows was not found. Install Git, then rerun bootstrap."
}

function Get-CMakeVersion([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return [version]"0.0.0"
    }
    $line = (& $Path --version | Select-Object -First 1)
    if ($line -match "(\d+\.\d+\.\d+)") {
        return [version]$Matches[1]
    }
    return [version]"0.0.0"
}

function Ensure-CMake {
    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null

    if (Test-Path -LiteralPath $CMakeExe) {
        return $CMakeExe
    }

    $systemCMake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($systemCMake) {
        $version = Get-CMakeVersion $systemCMake.Source
        if ($version -ge [version]"3.22.0") {
            return $systemCMake.Source
        }
    }

    $zipPath = Join-Path $ToolsDir "cmake-$CMakeVersion-windows-x86_64.zip"
    if (-not (Test-Path -LiteralPath $zipPath)) {
        $url = "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/cmake-$CMakeVersion-windows-x86_64.zip"
        Write-Host "Downloading project-local CMake $CMakeVersion from $url"
        Invoke-WebRequest -Uri $url -OutFile $zipPath
    }

    Expand-Archive -Path $zipPath -DestinationPath $ToolsDir -Force
    if (-not (Test-Path -LiteralPath $CMakeExe)) {
        throw "Project-local CMake extraction failed: $CMakeExe was not created."
    }
    return $CMakeExe
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
    return $null
}

function Get-VsInstances {
    $vswhere = Find-VsWhere
    if (-not $vswhere) {
        return @()
    }
    $json = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json
    if (-not $json) {
        return @()
    }
    return $json | ConvertFrom-Json
}

function Ensure-DependencyClone($Name, $Url, $Ref, $Commit, [bool] $RecurseSubmodules = $false) {
    $git = Find-RealGit
    $path = Join-Path $Root "external\$Name"
    if (Test-Path -LiteralPath (Join-Path $path ".git")) {
        $current = (& $git -C $path rev-parse HEAD).Trim()
        Write-Host "$Name already present at $current"
        return
    }

    New-Item -ItemType Directory -Force -Path (Join-Path $Root "external") | Out-Null
    Write-Host "Cloning $Name $Ref"
    if ($RecurseSubmodules) {
        & $git clone --depth 1 --branch $Ref --recurse-submodules $Url $path
    } else {
        & $git clone --depth 1 --branch $Ref $Url $path
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Could not clone $Name. Manual command: git clone --depth 1 --branch $Ref $Url external\$Name"
    }

    $currentAfter = (& $git -C $path rev-parse HEAD).Trim()
    if ($currentAfter -ne $Commit) {
        Write-Warning "$Name commit is $currentAfter, expected $Commit; attempting exact checkout"
        & $git -C $path fetch --depth 1 origin $Commit
        if ($LASTEXITCODE -ne 0) {
            throw "Could not fetch pinned $Name commit $Commit."
        }
        & $git -C $path checkout --detach $Commit
        if ($LASTEXITCODE -ne 0) {
            throw "Could not checkout pinned $Name commit $Commit."
        }
        if ($RecurseSubmodules) {
            & $git -C $path submodule update --init --recursive --depth 1
            if ($LASTEXITCODE -ne 0) {
                throw "Could not initialize $Name submodules."
            }
        }
    }
}

Set-Location $Root
Repair-ProcessPathEnvironment

Write-Section "Tools"
$gitPath = Find-RealGit
Write-Host "Git: $gitPath"
& $gitPath --version

$cmakePath = Ensure-CMake
Write-Host "CMake: $cmakePath"
& $cmakePath --version | Select-Object -First 1

$ninja = Get-Command ninja.exe -ErrorAction SilentlyContinue
if ($ninja) {
    Write-Host "Ninja: $($ninja.Source)"
} else {
    Write-Host "Ninja: not found (optional)"
}

Write-Host "PowerShell: $($PSVersionTable.PSVersion)"

Write-Section "Visual Studio"
$instances = @(Get-VsInstances)
if ($instances.Count -eq 0) {
    throw "No Visual Studio C++ toolchain found. Install Visual Studio Build Tools with Desktop development with C++."
}

foreach ($instance in $instances) {
    Write-Host "$($instance.displayName) $($instance.installationVersion) at $($instance.installationPath)"
}

if (-not ($instances | Where-Object { $_.installationVersion -like "18.*" -or $_.installationVersion -like "17.*" })) {
    Write-Host "VS 2026/2022 not found; using installed VS 2019/MSVC x64 because it is sufficient for this build."
}

Write-Section "Windows SDK"
$sdkRoots = @(
    "C:\Program Files (x86)\Windows Kits\10\bin",
    "C:\Program Files\Windows Kits\10\bin"
)
foreach ($sdkRoot in $sdkRoots) {
    if (Test-Path -LiteralPath $sdkRoot) {
        Write-Host "Windows SDK bin: $sdkRoot"
    }
}

Write-Section "Dependencies"
Ensure-DependencyClone "JUCE" "https://github.com/juce-framework/JUCE.git" "8.0.13" "7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2"
Ensure-DependencyClone "civetweb" "https://github.com/civetweb/civetweb.git" "v1.16" "d7ba35bbb649209c66e582d5a0244ba988a15159"
Ensure-DependencyClone "opus" "https://github.com/xiph/opus.git" "main" "3da9f7a6db1c05c3996cb363a9d1931a978bf1be"
Ensure-DependencyClone "libdatachannel" "https://github.com/paullouisageneau/libdatachannel.git" "master" "a542d8703bfab42a5533852e18d6d1879e01080a"
Ensure-DependencyClone "mbedtls" "https://github.com/Mbed-TLS/mbedtls.git" "mbedtls-3.6.6" "0bebf8b8c7f07abe3571ded48a11aa907a1ffb20" $true

Write-Section "Bootstrap Complete"
Write-Host "Project root: $Root"
