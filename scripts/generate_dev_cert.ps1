param(
    [string] $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Find-OpenSsl {
    $candidates = @(
        "C:\Program Files\OpenSSL-Win64\bin\openssl.exe",
        "C:\Program Files\Git\mingw64\bin\openssl.exe",
        "C:\Program Files\Git\usr\bin\openssl.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $command = Get-Command openssl.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "OpenSSL was not found. Install OpenSSL for Windows or Git for Windows, then rerun bootstrap."
}

function Get-LanAddresses {
    $addresses = @("127.0.0.1")
    try {
        $netAddresses = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction Stop |
            Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" } |
            Select-Object -ExpandProperty IPAddress
        foreach ($address in $netAddresses) {
            if ($addresses -notcontains $address) {
                $addresses += $address
            }
        }
    } catch {
        $ipconfig = ipconfig | Select-String -Pattern "IPv4"
        foreach ($line in $ipconfig) {
            if ($line -match "(\d+\.\d+\.\d+\.\d+)") {
                $address = $Matches[1]
                if ($address -notlike "169.254.*" -and $addresses -notcontains $address) {
                    $addresses += $address
                }
            }
        }
    }

    return $addresses
}

$certDir = Join-Path $Root "assets\certs"
New-Item -ItemType Directory -Force -Path $certDir | Out-Null

$certPath = Join-Path $certDir "dev-cert.pem"
$keyPath = Join-Path $certDir "dev-key.pem"
$configPath = Join-Path $certDir "dev-cert.cnf"

if ((Test-Path -LiteralPath $certPath) -and (Test-Path -LiteralPath $keyPath)) {
    Write-Host "Certificate already exists:"
    Write-Host "  $certPath"
    Write-Host "  $keyPath"
    exit 0
}

$openssl = Find-OpenSsl
$addresses = Get-LanAddresses
$altNames = @("DNS.1 = localhost")
$index = 1
foreach ($address in $addresses) {
    $altNames += "IP.$index = $address"
    $index += 1
}

$config = @"
[ req ]
default_bits = 2048
prompt = no
default_md = sha256
distinguished_name = dn
req_extensions = req_ext
x509_extensions = req_ext

[ dn ]
CN = PGStream Local Development
O = PigeonStream
OU = PGStream

[ req_ext ]
subjectAltName = @alt_names
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth

[ alt_names ]
$($altNames -join "`r`n")
"@

Set-Content -LiteralPath $configPath -Value $config -Encoding ASCII

Write-Host "Generating self-signed PGStream development certificate with $openssl"
& $openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes `
    -keyout $keyPath `
    -out $certPath `
    -subj "/CN=PGStream Local Development" `
    -config $configPath `
    -extensions req_ext

if ($LASTEXITCODE -ne 0) {
    throw "OpenSSL certificate generation failed."
}

Write-Host "Generated:"
Write-Host "  $certPath"
Write-Host "  $keyPath"

