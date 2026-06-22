# PGStream

PGStream is a Windows x64 VST3 audio effect for transparent stereo master-bus tapping. The DAW audio path is passed through 1:1; the plugin only copies the first stereo pair into a preallocated FIFO and streams that copy to a LAN browser.

The browser stream supports lossy Opus monitoring and lossless PCM16/PCM24/PCM32F monitoring modes. Opus is designed for perceptually transparent LAN listening; PCM modes use WebRTC DataChannel transport and browser AudioWorklet playback. PCM32F transports Float32 stereo samples without integer quantization inside the transport layer.

The embedded server is disabled by default for DAW scan safety. Enabling the stream starts a CivetWeb server inside the plugin. Self-signed HTTPS is enabled by default for browser secure-context APIs; disabling it falls back to HTTP/WS and forces Opus. WebSocket/WSS is used only for signaling/control, not PCM audio.

## Build

Requirements:

- Windows x64
- Visual Studio 2019 or newer with Desktop development with C++; Visual Studio 2026 is preferred when installed
- PowerShell
- Git for Windows

Bootstrap fetches pinned source dependencies into `external/` and prepares project-local tools when needed. No local TLS certificate or OpenSSL installation is required.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap_windows.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build_release_windows.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\verify_artifact_windows.ps1
```

The release script selects the newest installed native MSVC x64 toolchain and builds the normal Release preset. A project-local CMake 4.3.3 fallback is downloaded by bootstrap when the system CMake is older than JUCE requires.

## Artifacts

The complete VST3 bundles are copied to:

```text
.\dist\PGStream.vst3
.\PGStream.vst3
```

The root `.\PGStream.vst3` bundle is committed as the distributable VST3 artefact. Copy the whole `PGStream.vst3` folder, not only `Contents\x86_64-win\PGStream.vst3`.

Recommended manual destination:

```text
C:\Program Files\Common Files\VST3
```

Point the DAW scanner at the parent VST3 folder, not at `PGStream.vst3\Contents`.

## Browser Stream

Insert PGStream at the end of a master bus, enable streaming, then open the shown LAN URL, for example:

```text
https://<LAN-IP>:8123/
```

The browser page uses WSS for SDP/ICE signaling when served over HTTPS and WS when served over HTTP. The certificate is self-signed and generated locally with SANs for localhost, pigeonstream.local, 127.0.0.1, and the selected LAN IP.

The browser **Connect / Play** control toggles to **Stop** after playback setup begins. Stop closes the active WebRTC peer and allows another start in the same page session without a reload.

PGStream encodes stereo 48 kHz Opus frames on the network worker thread and sends them as RTP media through libdatachannel. SDP offers from the browser are answered by the plugin over WS signaling, and the sender uses the offered audio MID and Opus RTP payload type so the remote audio track is paired correctly.

Opus is configured for continuous stereo music/audio at the selected bitrate with DTX and in-band FEC disabled for LAN mode. Encoder complexity starts at 8 and can fall back internally to 6 only after the encode over-budget counter increases.

PCM16, PCM24, and PCM32F modes use a browser-owned WebRTC DataChannel named `pgs-pcm-audio` with unordered, max-retransmits-0 delivery. PCM packets use the `PGPC` 40-byte binary header, fixed 10 ms / 480-frame packets, 48 kHz stereo little-endian payloads, browser validation, a single browser playback ring buffer, and AudioWorklet output. PCM16 is about 1536 kb/s payload bitrate, PCM24 is about 2304 kb/s, and PCM32F is about 3072 kb/s. No PCM audio is sent over WebSocket/WSS.

The worker reads from the audio FIFO in roughly one Opus-frame chunk at a time. If the DAW session is not 48 kHz, resampling to 48 kHz happens only on the non-realtime network worker. The local DAW pass-through path is never resampled.

The browser and DAW do not share a hardware audio clock. PGStream relies on WebRTC's browser jitter buffer for Opus/RTP playout. When the DAW is idle and keep-alive is enabled, PGStream sends Opus silence frames so the WebRTC media path stays warm.

The project image `assets/pgs.png` is embedded in the plugin binary and shown in both the plugin editor and browser UI. The plugin editor renders a QR code for the selected primary LAN URL after the stream is enabled.

The plugin editor About panel opens from the small `i` button and closes with its `X` button. It displays the embedded `assets/logo.png` image, plugin name, version 0.8, author, description, copyright, project link, and AGPL license note.

The small `S` button opens Settings. In v0.8 Settings contains the self-signed HTTPS toggle. When self-signed HTTPS is off, PGStream serves the receiver over HTTP/WS, forces Engine to Opus, disables PCM choices in the browser, and shows the lossless-PCM HTTPS requirement in the UI.

The plugin port control is a numeric input, not a slider. It accepts ports from 1024 to 65535. PCM16, PCM24, and PCM32F show fixed read-only format/bitrate text instead of Opus bitrate choices: PCM16 48 kHz stereo is about 1536 kb/s, PCM24 is about 2304 kb/s, and PCM32F is about 3072 kb/s.

The browser normal view is intentionally compact: Connect / Play, Engine, Bitrate or PCM format, Latency Mode, Auto priority, Auto Negotiate, and Stats. Browser audio controls are hidden; Opus still uses an internal hidden audio element, while PCM uses AudioWorklet output. PCM creates an AudioContext with `latencyHint: "interactive"` and `sampleRate: 48000`; the actual sample rate, base latency, and output latency are shown in Stats. If the browser does not open a 48 kHz AudioContext, PCM is rejected instead of pretending to be sample-accurate.

## Browser Auto Negotiation

The browser page can run one-shot Auto Negotiation from the **Auto Negotiate** button. It tests explicit codec + latency candidates, reconnects when a candidate changes, waits until the peer connection is ready for the active engine, then waits for a 1 second warm-up and requires 2 continuous seconds with no receiver underruns, packet gaps, browser ring overflows, or sender dropped-before-send events. PCM readiness is based on DataChannel, AudioContext, AudioWorklet, and buffer state instead of remote media tracks/RTP counters.

The decision input is only browser WebRTC `packetsLost` delta:

```text
packetLossDelta = current packetsLost - baseline packetsLost
```

The baseline is captured after readiness and warm-up, separately for each tested peer/profile. A tested profile succeeds only when `packetLossDelta == 0` for the full 2 second evaluation window. Any `packetLossDelta > 0` after warm-up rejects that profile. Missing, undefined, or non-numeric packet-loss stats are treated as stats not ready; they do not reject a profile.

Modes choose candidates as follows:

- **Quality Priority**: test PCM32F through all latency targets, then PCM24 through all targets, then PCM16, then Opus.
- **Latency Priority**: test PCM32F, PCM24, PCM16, and Opus at one latency target before moving to the next higher target.

If stats remain unavailable, Auto Negotiation stops with a diagnostic instead of degrading blindly. If every valid profile fails, PGStream falls back to Opus with Very Safe latency. Auto Negotiation updates the active native state, and the plugin and browser Engine, bitrate, latency, and Auto priority controls follow the confirmed active value when the server broadcasts state. Plugin FIFO underruns are displayed as diagnostics only; they never trigger Auto Negotiation profile changes and do not affect the final selected profile.

## Nerd Diagnostics

The plugin editor and browser page each include a **Nerd** or **Stats** toggle. Diagnostics are collapsed by default.

Plugin-side diagnostics show a compact set of server and sender metrics:

- **Common**: active engine, connection state, connected clients, selected profile, last state-change reason, receiver ACK age, FIFO fill, FIFO underruns/drops, and encode over-budget count.
- **Opus**: RTP sent/failures and browser-side packet-loss/jitter direction.
- **PCM**: DataChannel open/ready counts, queued bytes, dropped-before-send count, PCM packets/bytes/failures, stream packet size, receiver buffer/target, receiver ACK age, missing/late/overflow counts, AudioContext sample rate/latencies, and receiver underflows.
- **LAN**: candidate LAN URLs are visible only in Nerd.

Browser-side Stats show a compact set of receiver metrics:

- **Common**: connection state, active engine, codec/format, actual throughput, RTT, and last fallback/reconnect reason.
- **Security**: `location.protocol`, secure-context status, AudioWorklet availability, AudioContext sample rate/base latency/output latency, signaling scheme, receiver scheme, and certificate mode.
- **Opus**: remote track, packets received/lost, jitter, concealed samples, and jitter-buffer values where the browser exposes them.
- **PCM**: secure context, AudioWorklet state, DataChannel state, queued bytes, stream id, sample rate, packet duration, PCM buffer/target, packets, drops, missing/late packets, underflows, ACK age, and last PCM reason.
- **Auto Negotiation**: active state, mode, tested profile, phase, stable zero-loss time, active bitrate, final profile, last failure, and FIFO underruns.

## PCM Manual Acceptance Checks

1. Enable streaming with self-signed HTTPS on, open the HTTPS LAN URL, select PCM16, and press **Connect / Play**. In browser Stats, verify DataChannel is open, AudioWorklet is ready, buffer rises above 0 ms, and no reconnect loop occurs because remote track/open RTP counters are zero.
2. Repeat with PCM24 and PCM32F. Confirm the normal view shows the correct fixed PCM bitrate and browser Stats show PCM packets increasing.
3. Select Opus. Confirm Opus still plays via WebRTC media/RTP and does not require the PCM DataChannel.
4. Change Latency Mode and confirm PCM packet duration remains 10 ms / 480 frames while target buffer fill changes.
5. Disable self-signed HTTPS in the plugin. Confirm the browser uses HTTP/WS, PCM options are disabled/forced to Opus, and Opus still works.
6. Open plugin Settings with `S`, toggle self-signed HTTPS, and confirm the receiver restarts to the matching HTTP/HTTPS URL without a DAW restart.
7. Edit the plugin Port field and confirm only ports 1024-65535 are accepted.
8. Open browser Stats and confirm normal-view metrics are hidden until Stats is expanded.
9. Run Auto Negotiation in Quality Priority and Latency Priority and confirm it stops after selecting a candidate.

## LAN IP Selection

The displayed LAN URL is selected from local Windows adapter data without internet lookup or admin rights. PGStream keeps the server bound broadly for reachability, but ranks display URLs as follows:

1. Private IPv4 on an active Wi-Fi/WLAN adapter with an IPv4 default gateway.
2. Private IPv4 on an active Ethernet adapter with an IPv4 default gateway.
3. Other active private IPv4 adapter with an IPv4 default gateway.
4. Other active private IPv4 adapter.
5. Other non-loopback IPv4 addresses.
6. `169.254.x.x` link-local addresses only as a last resort.

Loopback `127.0.0.1` is not used as the primary LAN URL unless no other usable address is found. Virtual and host-only adapters are downranked so they do not beat the real Wi-Fi/Ethernet LAN.

## WebRTC Signaling

WS JSON messages on `/ws`:

- browser to plugin: `webrtc-offer`, with SDP, bitrate, and latency preset
- plugin to browser: `webrtc-answer`
- both directions: `webrtc-candidate`
- browser to plugin: `webrtc-stop`

Audio media is sent from the plugin to the browser as Opus RTP over the WebRTC peer connection.

## Troubleshooting

If the DAW does not see the plugin, verify that the complete bundle is present under the parent VST3 scan folder and rescan that parent folder. Do not copy only the inner binary, and do not scan `PGStream.vst3\Contents`.

If the browser does not connect, enable the stream in the plugin, check the port, and confirm the computer firewall allows the DAW to accept LAN connections on the selected port.

If WebRTC shows **connected** but there is silence, open browser Stats and check whether `Packets received` increases. In the plugin Nerd view, check `WebRTC open tracks`, `WebRTC encoded packets`, `RTP sent`, and `RTP send failures`. A connected peer with zero sent packets usually points at sender/track setup or missing DAW input; increasing packets with silence points toward browser/device audio output, mute controls, or DAW routing before the plugin.

If the shown LAN URL is suspicious, run `ipconfig` and compare it with the plugin Nerd candidate URLs. Try `http://<real-LAN-IP>:8123/` using the active Wi-Fi or Ethernet IPv4 address.

## Repository Contents

The public repository contains source code, web assets, build scripts, documentation, and the root `PGStream.vst3` distributable bundle. It intentionally does not commit:

- duplicate `dist` VST3 output
- build directories
- downloaded JUCE/CivetWeb/Mbed TLS/libdatachannel/Opus/CMake dependencies
- private keys, certificates, or local binary SDK blobs

Run `scripts\bootstrap_windows.ps1` after cloning to recreate the local dependency tree.

## License

PGStream is licensed under AGPL-3.0-only. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
