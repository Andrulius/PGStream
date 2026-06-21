# Build Notes

PGStream is a native Windows x64 VST3 built with JUCE, CMake, CivetWeb, Mbed TLS, Opus, and libdatachannel. The DAW audio path remains transparent while browser streaming runs on a separate non-realtime network worker. The browser stream is lossy Opus for perceptually transparent monitoring/broadcast quality, not bit-perfect transmission.

## Source Dependencies

The repository does not vendor generated build folders or duplicate `dist` artefacts. The root `PGStream.vst3` bundle is tracked as the distributable VST3 artefact. Run the bootstrap script to fetch pinned dependencies:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap_windows.ps1
```

Pinned dependency versions:

- JUCE: `8.0.13`
- CivetWeb: `v1.16`
- Mbed TLS: `mbedtls-3.6.6`, commit `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`
- libdatachannel: commit `a542d8703bfab42a5533852e18d6d1879e01080a`
- Opus: commit `3da9f7a6db1c05c3996cb363a9d1931a978bf1be`
- CMake: `4.3.3` project-local fallback when the system CMake is too old

## LAN Transport And Crypto Backend

PGStream serves the browser UI over plain LAN HTTP and uses WS only for WebRTC SDP/ICE signaling. Audio media is WebRTC Opus RTP through libdatachannel; the build does not use the full Google libwebrtc reference library.

libdatachannel is built with Mbed TLS as the WebRTC DTLS/TLS backend. The active build does not require local OpenSSL headers, libraries, executables, generated certificates, or private keys.

PGStream adds one project-local Mbed TLS user config at `cmake\mbedtls_pgstream_config.h` to enable `MBEDTLS_SSL_DTLS_SRTP`, which libdatachannel requires for WebRTC SRTP profile negotiation.

## Release Build

Use the release build script:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_release_windows.ps1
```

The script selects the newest installed native MSVC x64 toolchain. With Visual Studio 2026 installed it uses the `vs2026-x64` configure preset and `vs2026-release` build preset. The plugin target no longer forces `/MP1`, `/Od`, or `/Ob0`; Release builds use the normal MSVC Release optimization profile.

Expected project-local outputs:

- `.\dist\PGStream.vst3`
- `.\PGStream.vst3`

Do not copy files outside the project as part of the build.

## Verification

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\verify_artifact_windows.ps1
```

The verification script checks:

- both project-local VST3 bundles exist
- `Contents\Resources\moduleinfo.json` exists
- `Contents\x86_64-win\PGStream.vst3` exists
- the inner binary is Windows x64 PE
- the bundle and inner binary names are consistent
- no `libssl` or `libcrypto` DLL dependency is present

`scripts\audit_windows_daw_visibility.ps1` is optional. It discovers validators, pluginval, and common DAWs, but may recursively scan `C:\Program Files` and `C:\Program Files (x86)`.

## Streaming Behavior

The browser transport is WebRTC Opus. The browser creates a recvonly audio transceiver; the plugin answers with a sendonly Opus track. The page does not call `getUserMedia`, so browser capture processing such as echo cancellation, noise suppression, and automatic gain control is not enabled. The sender parses the browser SDP offer and uses the offered audio MID and Opus RTP payload type when creating the outgoing track.

The network worker reads from the audio FIFO in approximately one Opus-frame-sized chunk, resamples to 48 kHz on the worker when needed, encodes Opus, and sends RTP through libdatachannel. Opus receives exact 10 ms or 20 ms stereo frames only. This avoids the old burst-prone behavior of draining large FIFO chunks into media sends.

The Opus encoder uses 48 kHz stereo `OPUS_APPLICATION_AUDIO`, music signal mode, constrained VBR at the active bitrate, DTX disabled, in-band FEC disabled, packet loss percentage 0, LSB depth 24, and default complexity 8. Complexity can fall back internally to 6 only after the encode over-budget counter increases.

The native side is authoritative for the active streaming bitrate and latency mode. Browser and plugin comboboxes are synchronized to confirmed active state updates, including Auto Negotiation changes, with programmatic update guards to avoid UI feedback loops.

RTP accounting is explicit:

- encoded packet counters advance only after a frame is successfully emitted to at least one open track
- RTP timestamp cursor advances only for audio actually sent to at least one open track
- sender diagnostics expose RTP attempts, successful sends, send failures, negotiated payload type, SSRC, sequence/timestamp cursor, submitted track packets/bytes, Opus packet size, and input RMS

FIFO polling is not treated as an underrun when there are simply not enough frames yet. Server FIFO underruns are reserved for real source starvation after the worker has seen source audio.

If keep-alive is enabled during real idle, Opus silence frames are sent at the selected WebRTC frame duration so the media path remains warm.

## Removed Legacy Path

The legacy WebSocket binary audio fallback was removed in version 0.5. Removed pieces include:

- browser transport selector for legacy mode
- AudioWorklet browser player
- `PGS1` binary WebSocket audio frame protocol
- C++ `WebSocketHub` broadcast path
- legacy packet format, sample-rate mode, packet-duration mode, and browser buffer-target parameters

WS remains in the application only as the WebRTC signaling channel.

## Realtime Safety

The audio thread does not perform network I/O, filesystem I/O, blocking waits, sleeps, resampling, Opus encoding, or WebRTC calls. It only passes audio through and writes the tapped stereo copy into the preallocated FIFO when streaming is enabled.

## LAN URL and QR

The server binds broadly for LAN reachability, while the displayed URL and QR code use a ranked local IPv4 selection:

1. Active private Wi-Fi adapter with IPv4 default gateway
2. Active private Ethernet adapter with IPv4 default gateway
3. Other active private adapter with gateway
4. Other active private adapter
5. Other non-loopback IPv4
6. `169.254.x.x` link-local only as a last resort

Virtual and host-only adapters are downranked. The QR code encodes the full HTTP URL shown in the plugin UI.
