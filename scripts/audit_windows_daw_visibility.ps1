$ErrorActionPreference = "Continue"

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

Repair-ProcessPathEnvironment

Write-Host "PGStream DAW visibility readiness audit"
Write-Host "Project root: $Root"
Write-Host ""

Write-Host "== Project-local artifact verification =="
& (Join-Path $PSScriptRoot "verify_artifact_windows.ps1")
$verifyExit = $LASTEXITCODE

Write-Host ""
Write-Host "== Validator discovery =="
$bundle = Join-Path $Root "dist\PGStream.vst3"

function Find-ExeByName([string[]] $Names, [string[]] $Roots) {
    $found = @()
    foreach ($rootPath in $Roots) {
        if (Test-Path -LiteralPath $rootPath) {
            foreach ($name in $Names) {
                $found += Get-ChildItem -Path $rootPath -Filter $name -Recurse -File -ErrorAction SilentlyContinue |
                    Select-Object -ExpandProperty FullName
            }
        }
    }
    foreach ($name in $Names) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) {
            $found += $command.Source
        }
    }
    return $found | Select-Object -Unique
}

$searchRoots = @(
    $Root,
    "C:\Program Files",
    "C:\Program Files (x86)"
)

$validators = Find-ExeByName @("validator.exe") $searchRoots
if ($validators.Count -gt 0) {
    foreach ($validator in $validators) {
        Write-Host "Found Steinberg-style validator: $validator"
        Write-Host "Command: $validator $bundle"
        & $validator $bundle
        Write-Host "Exit code: $LASTEXITCODE"
    }
} else {
    Write-Host "Steinberg VST3 validator not found. Host-style validator step not performed."
}

$moduleInfoTools = Find-ExeByName @("moduleinfotool.exe", "moduleinfotool") $searchRoots
if ($moduleInfoTools.Count -gt 0) {
    foreach ($tool in $moduleInfoTools) {
        Write-Host "Found moduleinfotool: $tool"
    }
} else {
    Write-Host "Steinberg moduleinfotool not found."
}

$pluginvals = Find-ExeByName @("pluginval.exe", "pluginval") $searchRoots
if ($pluginvals.Count -gt 0) {
    foreach ($pluginval in $pluginvals) {
        Write-Host "Found pluginval: $pluginval"
        Write-Host "Command: $pluginval --validate-in-process --strictness-level 5 --validate $bundle"
        & $pluginval --validate-in-process --strictness-level 5 --validate $bundle
        Write-Host "Exit code: $LASTEXITCODE"
    }
} else {
    Write-Host "pluginval not found. pluginval host smoke step not performed."
}

Write-Host ""
Write-Host "== DAW/host discovery =="
$dawPatterns = @(
    "reaper.exe",
    "Ableton Live*.exe",
    "Cubase*.exe",
    "Studio One.exe",
    "FL64.exe",
    "Bitwig Studio.exe",
    "Waveform*.exe",
    "Cakewalk.exe"
)

$dawRoots = @("C:\Program Files", "C:\Program Files (x86)")
$dawFound = @()
foreach ($rootPath in $dawRoots) {
    if (Test-Path -LiteralPath $rootPath) {
        foreach ($pattern in $dawPatterns) {
            $dawFound += Get-ChildItem -Path $rootPath -Filter $pattern -Recurse -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty FullName
        }
    }
}

if ($dawFound.Count -gt 0) {
    $dawFound | Select-Object -Unique | ForEach-Object { Write-Host "Found: $_" }
    Write-Host "No DAW cache or plugin folder was modified. No automatic DAW scan was performed."
} else {
    Write-Host "No common DAW or safe command-line VST3 host was discovered."
}

Write-Host ""
Write-Host "== Readiness report =="
if ($verifyExit -eq 0) {
    Write-Host "Project-local artifact readiness: PASS"
} else {
    Write-Host "Project-local artifact readiness: FAIL"
}

if ($validators.Count -gt 0 -or $pluginvals.Count -gt 0) {
    Write-Host "Validator/host readiness: attempted with discovered tool(s); inspect exit codes above."
} else {
    Write-Host "Validator/host readiness: NOT RUN - no validator/pluginval discovered."
}

Write-Host "Actual DAW scan: NOT PERFORMED - the audit does not copy plugins or modify DAW settings."
Write-Host "Manual next step: copy the complete .\dist\PGStream.vst3 folder to a VST3 parent folder and rescan that parent folder in the DAW."

if ($verifyExit -ne 0) {
    exit 1
}
exit 0
