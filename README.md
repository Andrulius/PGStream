# PGStream

PGStream is a Windows x64 VST3 audio effect for transparent stereo master-bus tapping. The DAW audio path is passed through 1:1; the plugin only copies the first stereo pair into a preallocated FIFO and streams that copy to a LAN browser.

The embedded server is disabled by default for DAW scan safety. Enabling the stream starts a plain HTTP/WS CivetWeb server inside the plugin. The browser page is embedded HTML/CSS/JS. Audio transport is WebRTC Opus over libdatachannel with Mbed TLS for DTLS/SRTP; WebSocket is used only for WebRTC signaling.

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
http://<LAN-IP>:8123/
```

The browser page is served over plain LAN HTTP and uses a plain WS connection for SDP/ICE signaling. Use it only on a trusted local network.

The browser **Connect / Play** control toggles to **Stop** after playback setup begins. Stop closes the active WebRTC peer and allows another start in the same page session without a reload.

PGStream encodes stereo 48 kHz Opus frames on the network worker thread and sends them as RTP media through libdatachannel. SDP offers from the browser are answered by the plugin over WS signaling, and the sender uses the offered audio MID and Opus RTP payload type so the remote audio track is paired correctly.

The worker reads from the audio FIFO in roughly one Opus-frame chunk at a time. If the DAW session is not 48 kHz, resampling to 48 kHz happens only on the non-realtime network worker. The local DAW pass-through path is never resampled.

The browser and DAW do not share a hardware audio clock. PGStream relies on WebRTC's browser jitter buffer for Opus/RTP playout. When the DAW is idle and keep-alive is enabled, PGStream sends Opus silence frames so the WebRTC media path stays warm.

The project image `assets/pgs.png` is embedded in the plugin binary and shown in both the plugin editor and browser UI. The plugin editor renders a QR code for the selected primary LAN URL after the stream is enabled.

The plugin editor About panel opens from the small `i` button and closes with its `X` button. It displays the embedded `assets/logo.png` image, plugin name, version 0.5, author, description, copyright, project link, and AGPL license note.

## Browser Auto Negotiation

The browser page can automatically test the existing **Opus Bitrate** and **Latency Mode** controls. It starts from 510 kb/s with Ultra Low latency, reconnects when a profile changes, waits until the peer connection is connected, the remote audio track is live, inbound audio stats exist, `packetsReceived` is increasing, and `packetsLost` is readable. It then waits for a 1 second warm-up and requires 3 continuous seconds with zero browser WebRTC packet loss delta.

The decision input is only browser WebRTC `packetsLost` delta:

```text
packetLossDelta = current packetsLost - baseline packetsLost
```

The baseline is captured after readiness and warm-up, separately for each tested peer/profile. A tested profile succeeds only when `packetLossDelta == 0` for the full 3 second evaluation window. Any `packetLossDelta > 0` after warm-up rejects that profile. Missing, undefined, or non-numeric packet-loss stats are treated as stats not ready; they do not reject a profile.

Modes choose the next existing profile as follows:

- **Quality Priority**: increase Latency Mode first, lower bitrate only when latency is already at Safe.
- **Latency Priority**: lower bitrate first, increase Latency Mode only when bitrate is already at the lowest setting.
- **Balanced**: alternate one latency step safer, then one bitrate step lower.

If stats remain unavailable, Auto Negotiation stops with a diagnostic instead of degrading blindly. If every valid profile fails, PGStream selects the safest available settings: 128 kb/s with Safe latency. Plugin FIFO underruns are displayed as diagnostics only; they never trigger Auto Negotiation profile changes and do not affect the final selected profile.

## Nerd Diagnostics

The plugin editor and browser page each include a **Nerd** or **Stats** toggle. Diagnostics are collapsed by default.

Plugin-side diagnostics show server and WebRTC sender metrics:

- **Server FIFO underruns**: source starvation after the worker waited for audio. Ordinary "not enough frames yet" polling is not counted as an underrun.
- **WebRTC peers/open tracks**: current libdatachannel peers and open media tracks.
- **WebRTC encoded packets**: Opus encoder output successfully emitted to at least one open track.
- **RTP attempts/sent/failures**: send attempts to WebRTC tracks, successful sends, and exceptions from the media track.
- **Current selected LAN IP**, bind/listen address, port, input sample rate, total submitted 48 kHz frames, FIFO fill, FIFO drops, and candidate LAN URLs.

Browser-side diagnostics show:

- **WebRTC connectionState/iceConnectionState**: browser peer connection state.
- **Packets received/lost, jitter, RTT, concealed samples, jitter buffer delay**: browser WebRTC inbound audio stats.
- **Remote track**: browser audio track state.
- **Signaling socket**: the WS connection used only for WebRTC signaling.
- **RTP attempts/sent/failures**: plugin-side sender counters mirrored from `/info`.
- **Auto Negotiation**: active state, selected mode, tested profile, phase, baseline/current browser `packetsLost`, packet loss delta, stable zero-loss time, active encoder bitrate, active Opus frame duration, profile changes, final selected profile, last rejected profile, and FIFO underruns as a separate diagnostic.

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
