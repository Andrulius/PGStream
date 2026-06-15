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

function Ensure-DependencyClone($Name, $Url, $Tag, $Commit) {
    $git = Find-RealGit
    $path = Join-Path $Root "external\$Name"
    if (Test-Path -LiteralPath (Join-Path $path ".git")) {
        $current = (& $git -C $path rev-parse HEAD).Trim()
        Write-Host "$Name already present at $current"
        return
    }

    New-Item -ItemType Directory -Force -Path (Join-Path $Root "external") | Out-Null
    Write-Host "Cloning $Name $Tag"
    & $git clone --depth 1 --branch $Tag $Url $path
    if ($LASTEXITCODE -ne 0) {
        throw "Could not clone $Name. Manual command: git clone --depth 1 --branch $Tag $Url external\$Name"
    }

    $currentAfter = (& $git -C $path rev-parse HEAD).Trim()
    if ($currentAfter -ne $Commit) {
        Write-Warning "$Name commit is $currentAfter, expected $Commit"
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

if (-not ($instances | Where-Object { $_.installationVersion -like "17.*" })) {
    Write-Host "VS 2022 not found; using installed VS 2019/MSVC x64 because it is sufficient for this build."
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

Write-Section "OpenSSL"
$opensslExe = @(
    "C:\Program Files\OpenSSL-Win64\bin\openssl.exe",
    "C:\Program Files\Git\mingw64\bin\openssl.exe",
    "C:\Program Files\Git\usr\bin\openssl.exe"
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

if (-not $opensslExe) {
    throw "OpenSSL executable not found. Install OpenSSL for Windows or Git for Windows."
}
Write-Host "OpenSSL: $opensslExe"
& $opensslExe version

$opensslHeaderRoots = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\Extensions\Microsoft\Python\Miniconda\Miniconda3-x64\Library\include",
    "C:\Program Files\OpenSSL-Win64\include"
)
$opensslHeaderRoot = $opensslHeaderRoots | Where-Object { Test-Path -LiteralPath (Join-Path $_ "openssl\ssl.h") } | Select-Object -First 1
if (-not $opensslHeaderRoot) {
    throw "OpenSSL headers were not found. Install OpenSSL-Win64 developer files."
}
Write-Host "OpenSSL headers: $opensslHeaderRoot"
if (-not (Test-Path -LiteralPath "C:\Program Files\OpenSSL-Win64\lib\libssl_static.lib")) {
    throw "OpenSSL static libraries were not found under C:\Program Files\OpenSSL-Win64\lib."
}
Write-Host "OpenSSL static libraries: C:\Program Files\OpenSSL-Win64\lib"

Write-Section "Dependencies"
Ensure-DependencyClone "JUCE" "https://github.com/juce-framework/JUCE.git" "8.0.13" "7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2"
Ensure-DependencyClone "civetweb" "https://github.com/civetweb/civetweb.git" "v1.16" "d7ba35bbb649209c66e582d5a0244ba988a15159"

Write-Section "Certificate"
& (Join-Path $PSScriptRoot "generate_dev_cert.ps1") -Root $Root

Write-Section "Bootstrap Complete"
Write-Host "Project root: $Root"
