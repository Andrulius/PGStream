<p align="center">
  <img src="assets/logo.png" alt="PGStream logo" width="360">
</p>

# PGStream 0.9

PGStream is a Windows x64 VST3 plugin for listening to your DAW master bus from a phone, tablet, or another browser on the same LAN.

Put the plugin on your master bus, enable the stream, scan the QR code, and press **Connect / Play** in the browser. The DAW audio still passes through normally; PGStream only copies the stereo signal for network playback.

## What It Does

- Streams stereo DAW audio to a local browser.
- Runs its own small LAN web server inside the plugin.
- Shows a QR code for quick phone connection.
- Supports Opus for efficient monitoring and PCM16/PCM24/PCM32F for lossless studio-style monitoring.
- Keeps normal users away from noisy diagnostics; detailed status is behind **Nerd** in the plugin and **Stats** in the browser.

PGStream is intended for trusted local networks. It is not a public internet streaming server.

## Download / Install

The repository includes a built VST3 bundle at:

```text
PGStream.vst3
```

Copy the whole `PGStream.vst3` folder to your VST3 folder, usually:

```text
C:\Program Files\Common Files\VST3
```

Then rescan plugins in your DAW. Copy the whole folder, not only the inner `.vst3` binary.

## Basic Use

1. Add PGStream to the end of your master bus.
2. Turn on **Enable Stream**.
3. Scan the QR code or open the shown LAN URL.
4. In the browser, choose an engine and latency preset.
5. Press **Connect / Play**.

Default port: `8123`.

The Settings button (`S`) contains the self-signed HTTPS toggle. HTTPS is enabled by default because modern browsers require a secure context for the PCM AudioWorklet path. Your browser may warn about the local self-signed certificate; continue only when the IP address matches your computer and you trust the LAN.

## Stream Modes

**Opus WebRTC** is the practical everyday mode. It uses less bandwidth and relies on the browser's normal WebRTC audio path.

**PCM16 / PCM24 / PCM32F DataChannel** are lossless monitoring modes. They use a WebRTC DataChannel and browser AudioWorklet playback. PCM is intentionally honest: if a packet is missing, that audio is missing or silent and the drop is counted. PGStream does not hide missing audio with interpolation, crossfades, repeated samples, time stretching, or other repair tricks.

Approximate PCM payload bitrates at 48 kHz stereo:

- PCM16: 1536 kb/s
- PCM24: 2304 kb/s
- PCM32F: 3072 kb/s

## Latency Presets

The latency preset affects both PCM packet size and the browser safety buffer:

| Preset | PCM packet | Target/resume buffer | Ring capacity |
| --- | ---: | ---: | ---: |
| Ultra Low / Low | 10 ms | 60 ms | 250 ms |
| Medium | 20 ms | 100 ms | 400 ms |
| Safe | 40 ms | 180 ms | 700 ms |
| Very Safe | 100 ms | 300 ms | 1000 ms |

Use **Low** when the network and browser are stable. Use **Safe** or **Very Safe** for phones, busy browsers, or diagnostics.

## Auto Negotiation

The browser can try profiles automatically with **Auto Negotiate**.

- **Quality Priority** tries the highest-quality PCM modes first.
- **Latency Priority** tries lower-latency profiles first.

Auto Negotiation watches real receiver failures such as missing packets, late packets, underruns, overflow, and packet loss. It does not silently change profiles based on plugin FIFO diagnostics alone.

## Troubleshooting

If the DAW does not see PGStream, rescan the parent VST3 folder and verify that the complete `PGStream.vst3` folder is installed.

If the browser cannot open the page, check that streaming is enabled, the port is correct, and Windows Firewall allows the DAW to accept LAN connections.

If HTTPS shows a warning, verify that the IP address belongs to your computer. The certificate is generated locally and is expected to be self-signed.

If audio is silent, open **Stats** in the browser and **Nerd** in the plugin. Check whether packets are moving, whether the DataChannel or WebRTC state is connected, and whether the browser is reporting underruns or missing packets.

## Build From Source

Requirements:

- Windows x64
- Visual Studio 2019 or newer with C++ desktop tools
- PowerShell
- Git for Windows

Build:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap_windows.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build_release_windows.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\verify_artifact_windows.ps1
```

The release build writes complete bundles to:

```text
dist\PGStream.vst3
PGStream.vst3
```

Downloaded dependencies and build folders are not tracked in git. Run the bootstrap script after cloning.

## Repository Contents

Tracked:

- source code
- browser UI assets
- build scripts
- documentation
- root `PGStream.vst3` distributable bundle

Not tracked:

- build folders
- duplicate `dist` output
- downloaded dependency checkouts
- local certificates and private keys
- local SDK or tool cache binaries

## License

PGStream is licensed under **AGPL-3.0-only**. See [LICENSE](LICENSE).

Third-party dependency notices are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The main dependencies are JUCE, CivetWeb, Mbed TLS, libdatachannel, Opus, and small libraries bundled by libdatachannel. The build uses JUCE under the AGPL option.
