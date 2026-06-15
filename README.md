# PGStream

PGStream is a Windows x64 VST3 audio effect for transparent stereo master-bus tapping. The DAW audio path is passed through 1:1; the plugin only copies the first stereo pair into a preallocated lock-free FIFO and streams that copy to LAN browser clients.

The embedded server is disabled by default for DAW scan safety. Enabling the stream starts an HTTPS/WSS CivetWeb server inside the plugin. The browser page is plain embedded HTML/CSS/JS and uses Web Audio with an AudioWorklet, so the listener must press **Start Audio**.

## Build

Requirements:

- Windows x64
- Visual Studio 2019 or newer with Desktop development with C++
- PowerShell
- Git for Windows
- OpenSSL for Windows or the OpenSSL executable bundled with Git for Windows

Bootstrap fetches pinned source dependencies into `external/`, prepares project-local tools when needed, and generates a local self-signed development certificate under `assets/certs/`. Those generated files are intentionally ignored by git.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap_windows.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build_release_windows.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\verify_artifact_windows.ps1
```

The build uses the installed native MSVC x64 toolchain. A project-local CMake 4.3.3 fallback is downloaded by bootstrap when the system CMake is older than JUCE requires.

Optional local readiness audit:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\audit_windows_daw_visibility.ps1
```

The audit may recursively search common Windows program folders for validators and DAWs; it does not install or copy the plugin.

## Artifacts

The complete VST3 bundles are copied to:

```text
.\dist\PGStream.vst3
.\PGStream.vst3
```

Build artefacts and generated VST3 bundles are not committed to the repository.

Copy the whole `PGStream.vst3` folder, not only `Contents\x86_64-win\PGStream.vst3`.

Recommended manual destination:

```text
C:\Program Files\Common Files\VST3
```

Alternative per-user destination:

```text
%LOCALAPPDATA%\Programs\Common\VST3
```

Point the DAW scanner at the parent VST3 folder, not at `PGStream.vst3\Contents`.

## Browser Stream

Insert PGStream at the end of a master bus, enable streaming, then open the shown LAN URL, for example:

```text
https://<LAN-IP>:8123/
```

Browsers require a secure context for AudioWorklet on LAN clients, so PGStream uses HTTPS and WSS. The certificate is self-signed, embedded in the plugin binary, and must be accepted manually in the browser.

The self-signed certificate can still appear as **not trusted**. That is expected unless the certificate or a local certificate authority is installed in the client device trust store. PGStream keeps HTTPS/WSS because plain HTTP over LAN is not a secure context in most browsers and can prevent AudioWorklet playback on phones.

The browser **Start Audio** control toggles to **Stop Audio** after playback setup begins. Stop closes the WebSocket, resets the client buffer and AudioWorklet state, and allows another start in the same page session without a reload.

Normal server mode aggregates DAW-tapped audio into complete WSS audio packets before sending. The default packet duration is 20 ms, which is 960 stereo frames at 48 kHz, 882 stereo frames at 44.1 kHz, and 1920 stereo frames at 96 kHz. The plugin also exposes 40 ms and 60 ms packet modes. The experimental **extr666me** mode sends complete 5 ms packets for minimum latency experiments, is not the default, and still keeps network I/O off the audio thread.

Browser playback starts in a controlled buffering state. The AudioWorklet outputs silence until the client buffer reaches the configured stream buffer target from the plugin metadata, or 100 ms if metadata is not available. Available plugin buffer targets are 20, 40, 60, 100, 250, 500, and 1000 ms. If jitter drains the client buffer below the critical threshold, the browser enters resync, outputs silence, keeps receiving packets, and resumes once the buffer has rebuilt to the selected target. If the client buffer grows far beyond the selected target plus a small tolerance, the oldest queued audio is trimmed so latency does not silently climb toward seconds unless a large target was selected.

The browser and DAW do not share a hardware audio clock over WSS. PGStream handles this by packetizing audio by source frames on the server, buffering by the selected latency target in the AudioWorklet, and using resync/trim behavior when the browser clock and network delivery drift apart.

The project image `assets/pgs.png` is embedded in the plugin binary. It is displayed near the top-right of the plugin editor at about 160 px and in the browser UI up to about 180 px, moving to the top center on narrow screens.

The plugin editor renders a QR code at the top, to the left of the `pgs.png` logo, for the selected primary LAN URL after the stream is enabled. The encoded QR text is the full HTTPS URL, for example `https://<LAN-IP>:8123/`, and matches the primary LAN URL shown in the editor.

## Nerd Diagnostics

The plugin editor and browser page each include a **Nerd** toggle. Diagnostics are collapsed by default.

Plugin-side diagnostics show server metrics only:

- **Server FIFO underruns**: source starvation after the worker waited for enough frames to form a complete packet. Ordinary "not enough frames yet" polling is not counted as an underrun.
- **Network packets sent**: successful WebSocket audio packet sends across connected clients.
- **WebSocket send failures**: failed WebSocket sends detected by the server.
- **Connected clients**: current browser clients registered with the embedded WebSocket hub.
- **Current selected LAN IP**, bind/listen address, port, stream format, stream sample rate, total frames sent, and candidate LAN URLs.

Browser-side diagnostics distinguish client metrics from server metrics:

- **Client AudioWorklet underruns**: the worklet reached a starvation/resync condition.
- **Client sequence gaps**: missing or out-of-order stream packet sequence numbers observed in the browser.
- **Client buffer fill ms**: queued client audio duration inside the AudioWorklet.
- **Packets/s**: browser receive rate measured over a rolling window.
- Stream sample rate, AudioContext sample rate, format, WebSocket state, received packet count, decoded frame count, resync count, and last packet sequence.

The old ambiguous **Buffer underruns** label has been replaced with explicit server/client labels.

## LAN IP Selection

The displayed LAN URL is selected from local Windows adapter data without internet lookup or admin rights. PGStream keeps the server bound broadly for reachability, but ranks display URLs as follows:

1. Private IPv4 on an active Wi-Fi/WLAN adapter with an IPv4 default gateway.
2. Private IPv4 on an active Ethernet adapter with an IPv4 default gateway.
3. Other active private IPv4 adapter with an IPv4 default gateway.
4. Other active private IPv4 adapter.
5. Other non-loopback IPv4 addresses.
6. `169.254.x.x` link-local addresses only as a last resort.

Loopback `127.0.0.1` is not used as the primary LAN URL unless no other usable address is found. Link-local `169.254.x.x` addresses are ignored whenever a better non-loopback IPv4 address exists. Adapter names/descriptions containing VirtualBox, VMware, Hyper-V, vEthernet, Docker, WSL, Loopback, TAP, TUN, VPN, or Host-Only are downranked so host-only/virtual adapters such as `192.168.56.1` do not beat the real Wi-Fi/Ethernet LAN. If several sensible LAN addresses are detected, the plugin Nerd section lists the candidate URLs and marks the selected primary URL. The selection is refreshed while the server runs, so the primary URL and QR can update after an IP change.

## Protocol

Audio is sent as binary WSS frames:

```text
4 bytes  magic "PGS1"
uint32   protocolVersion = 1
uint32   sequence
uint32   sampleRate
uint16   channels = 2
uint16   format: 1 Float32 LE, 2 PCM16 LE
uint32   frameCount
uint32   flags: bit 0 silence keep-alive
payload  interleaved stereo frames
```

v1 supports Float32 little-endian and PCM16 little-endian only. In normal packet modes the server sends only complete packet payloads at the selected duration; it does not send partial DAW blocks as standalone network packets. If the stream sample rate differs from the DAW session, resampling happens only on the non-realtime network worker thread. The local DAW pass-through path is never resampled.

## Troubleshooting

If the DAW does not see the plugin, verify that the complete bundle is present under the parent VST3 scan folder and rescan that parent folder. Do not copy only the inner binary, and do not scan `PGStream.vst3\Contents`.

If the browser does not connect, enable the stream in the plugin, check the port, accept the self-signed certificate warning, and confirm the computer firewall allows the DAW to accept LAN connections on the selected port.

If the shown LAN URL is suspicious, run `ipconfig` and compare it with the plugin Nerd candidate URLs. Try `https://<real-LAN-IP>:8123/` using the active Wi-Fi or Ethernet IPv4 address. Avoid `169.254.x.x` unless no private LAN address exists.

Unstable Wi-Fi may still require a larger browser buffer target such as 250, 500, or 1000 ms. Use 60 ms or lower only when the phone and computer have a clean LAN path.

## Repository Contents

The public repository contains source code, web assets, build scripts, and documentation. It intentionally does not commit:

- generated VST3 bundles
- build directories
- downloaded JUCE/CivetWeb/CMake dependencies
- generated development TLS private keys or certificates

Run `scripts\bootstrap_windows.ps1` after cloning to recreate the local generated pieces.

## License

This repository is publicly visible, but no open-source license is granted unless a separate license is added by the copyright holder. See `LICENSE`.
