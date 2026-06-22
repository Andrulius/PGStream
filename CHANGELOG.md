# Changelog

## PGStream lossless PCM DataChannel and HTTPS secure-context support - version 0.8

Files changed:

- Native: `src/StreamTypes.h`, `src/WebRtcAudioSender.*`, `src/NetworkServer.*`, `src/PluginState.*`, `src/PluginEditor.*`, `src/PluginProcessor.*`, `src/SelfSignedCertificate.*`, `src/HtmlAssets.cpp`, `src/NetworkInterfaceUtils.*`, `CMakeLists.txt`.
- Browser: `assets/web/index.html`, `assets/web/app.js`, `assets/web/pcm-worklet.js`, `assets/web/style.css`.
- Documentation and artifacts: `README.md`, `CHANGELOG.md`, root `PGStream.vst3`, `dist/PGStream.vst3`.

Added:

- Added stream engine selection for Opus, PCM16 DataChannel, PCM24 DataChannel, PCM32F DataChannel, and Auto.
- Added PCM32F as the high-quality Float32 transport; it sends 48 kHz stereo Float32 samples without integer quantization in the PCM transport layer.
- Added self-signed HTTPS support using the bundled Mbed TLS stack and CivetWeb TLS mode.
- Added browser-owned WebRTC DataChannel transport for PCM with label `pgs-pcm-audio`, unordered delivery, and no retransmission.
- Added PCM `PGPC` 40-byte packet format with version, codec, channels, header size, sample rate, sequence, split 64-bit sample time, frame count, payload byte count, stream id, and flags.
- Added fixed 10 ms / 480-frame PCM packets at 48 kHz for PCM16LE, PCM24LE, and PCM32FLE.
- Added browser AudioWorklet playback with one SharedArrayBuffer-backed planar Float32 ring buffer. The ring is also the PCM playout/jitter buffer; there is no separate reorder buffer.
- Added AudioContext setup for PCM with `latencyHint: "interactive"` and requested `sampleRate: 48000`, plus visible actual sample rate/base latency/output latency diagnostics.
- Added Media Session metadata and playback-state handling for active/stopped browser playback.
- Added PCM receiver readiness ACK over the existing signaling/control path so native sends full PCM only after DataChannel, AudioContext, AudioWorklet, ring buffer, and secure context are ready.
- Added PCM DataChannel backpressure handling and visible dropped-before-send counters.
- Added latency-mode target-fill control for PCM: Ultra Low 20 ms, Low 40 ms, Medium 70 ms, Safe 120 ms, Very Safe 180 ms.
- Added Audio Passthrough and self-signed HTTPS plugin parameters.
- Added plugin Settings popup behind a small `S` button; self-signed HTTPS is configured there instead of in the main UI.
- Replaced the plugin port slider with a validated numeric port input.
- Added plugin PCM bitrate readouts so PCM16/PCM24/PCM32F no longer show Opus bitrate labels.
- Added best-effort plugin Always on top toggle.
- Added browser PCM bitrate readout, AudioContext diagnostics, PCM stream id, sample rate, packet duration, queue, missing/late, overflow, and underflow stats.
- Added Engine and Auto priority synchronization through the existing `stream_state_update`/control path.
- Added one-shot browser Auto Negotiation. Quality priority tests PCM32F through all latency targets, then PCM24, then PCM16, then Opus. Latency priority tests PCM32F/PCM24/PCM16/Opus at each latency before moving higher.

Changed:

- Normal plugin and browser views are more compact; extended diagnostics are collapsed behind Nerd/Stats.
- PCM latency modes now change browser target fill only; PCM packet size stays fixed at 480 frames.
- HTTP/WS mode now forces both selected and active engine state to Opus instead of leaving selected PCM with active Opus.
- Browser manual PCM selection in a non-secure context now reports an error instead of silently starting Opus.
- Browser manual PCM setup rejects a non-48 kHz AudioContext instead of pretending PCM is sample-accurate without resampling.
- Browser Stats include secure-context diagnostics for receiver protocol, signaling scheme, AudioWorklet availability, certificate mode, and AudioContext timing.

Removed:

- Removed the separate browser PCM packet reorder/jitter buffer from this branch.
- Removed the PCM AudioWorklet queue fallback path. PCM now requires SharedArrayBuffer/cross-origin isolation and uses the single playback ring.
- Removed latency-dependent PCM packet sizing from this branch.

Preserved:

- Opus remains on the existing WebRTC media/RTP path.
- WebSocket/WSS remains signaling/control only; PCM audio is not sent over WebSocket.
- The DAW realtime audio thread still only pushes into the existing preallocated FIFO before optional passthrough mute.
- The existing Opus connection/signaling flow was kept in place while PCM uses its separate DataChannel audio path.

Audit:

- The branch already contained partial PCM16/PCM24 DataChannel code, browser parsing/playback code, and PCM UI/state plumbing.
- Existing HTTPS, Settings, port validation, LAN display, and UI polish from the v0.8 branch were reused.
- The old partial PCM packet format used `PGSP`, supported PCM16/PCM24 only, changed packet sizing with latency, and had a separate browser packet buffer before playback.
- Missing pieces were PCM32F, the final `PGPC` 40-byte header, fixed 480-frame packets, one-ring browser playback, AudioContext diagnostics, Media Session state, backpressure drops, and final Auto Negotiation ordering.
- The old WebSocket binary audio fallback and AudioWorklet player were documented as removed in v0.7.
- The final v0.8 implementation uses one coherent PCM path: plugin FIFO, PCM packetizer, WebRTC DataChannel, browser parser, browser playback ring buffer, AudioWorklet, output.

Known limitations:

- Runtime LAN audibility for PCM16/PCM24/PCM32F still needs manual DAW/browser testing.
- PCM requires HTTPS secure context, cross-origin isolation, SharedArrayBuffer, AudioWorklet, and an actual 48 kHz AudioContext.
- Browser control/config remains on the existing WebSocket/WSS signaling path instead of a separate reliable WebRTC control DataChannel.
- No PLC, resampling, time stretching, dynamic speed correction, or continuous background quality switching was added.

Tests:

- Built Release with `scripts/build_release_windows.ps1`.
- Verified `dist\PGStream.vst3` and root `PGStream.vst3` with `scripts\verify_artifact_windows.ps1`.
- Checked browser JavaScript syntax with `node --check` for `assets\web\app.js` and `assets\web\pcm-worklet.js`.
- Checked repository whitespace with `git diff --check`.

## PGStream WebRTC media path and Auto Negotiate UI sync fix - version 0.7

Fixed:

- Added confirmed active-state synchronization so Auto Negotiate bitrate/latency changes are reflected in both plugin and browser comboboxes.
- Added browser-side guarded programmatic combobox updates to prevent feedback loops during remote/native state sync.
- Hardened browser WebRTC receive setup by explicitly keeping the audio element unmuted, autoplaying, and attached only to live audio tracks.
- Added validation and diagnostics for negotiated Opus payload type, SSRC, RTP sequence/timestamp, submitted track packets/bytes, Opus packet size, input RMS, browser inbound bytes, browser track muted/live state, and browser audio level.

State model:

- Native side is the authority for active streaming state.
- Plugin and browser UI display confirmed active state.
- Auto Negotiate updates active native state and broadcasts `stream_state_update` messages to browser clients.
- Programmatic UI sync uses suppression guards and state revisions to avoid feedback loops.

Non-regression:

- No UI redesign.
- No presets added.
- No profiles added.
- No new bitrate values added.
- No new latency values added.
- WebSocket remains signaling/control/stats/state sync only.
- Audio transport remains WebRTC.
- Browser remains receive-only.

Tests:

- Automated build and artifact verification listed in the final validation notes for this pass.
- Manual LAN audibility and packet-loss validation were not run in this coding session.

## PGStream Audio Engine WebRTC/Opus Stabilization - version 0.6

Architecture:

- Kept libdatachannel as the native WebRTC stack to avoid the size and build complexity of the full Google libwebrtc reference library.
- Kept WebRTC Opus as the only browser audio transport; WebSocket remains signaling/control/stats only.
- Kept the browser receive-only with native RTCPeerConnection audio playback.
- Documented the stream as lossy Opus for perceptually transparent monitoring/broadcast quality, not bit-perfect transmission.

Audio engine:

- Kept audio capture in the DAW callback limited to the preallocated FIFO path.
- Kept encoder and WebRTC work on the non-realtime network worker.
- Ensured Ultra Low also uses a legal 10 ms Opus frame so Opus receives only exact 10 ms or 20 ms frames.

Opus:

- Configured libopus for 48000 Hz stereo `OPUS_APPLICATION_AUDIO`, music signal mode, constrained VBR, DTX disabled, in-band FEC disabled, packet loss percentage 0, and LSB depth 24.
- Set default Opus complexity to 8.
- Added internal fallback to complexity 6 only when `encodeOverBudgetCount` increases.
- Kept Opus complexity internal and out of the UI.

RTP/WebRTC:

- Added SDP `ptime` and `maxptime` matching the active 10 ms or 20 ms Opus frame duration.
- Preserved negotiated Opus payload type and offered browser audio MID handling.
- Kept RTP packet emission through the libdatachannel media track.

Diagnostics:

- Renamed the sender overload diagnostic to `encodeOverBudgetCount` while keeping the old JSON alias for compatibility.
- Exposed selected and actual bitrate fields in `/info` diagnostics.
- Browser stats polling now runs at 1 Hz by default.

Autonegotiation:

- Preserved existing UI modes and option names.
- Auto-negotiation now changes runtime profile tests without overwriting visible bitrate or latency controls.
- Manual browser profile changes can still update selected plugin parameters.

Non-regression:

- No UI layout redesign.
- No presets added.
- No profiles added.
- Existing bitrate controls preserved.
- Existing latency controls preserved.
- Existing autonegotiation controls preserved.
- Popup info version incremented only.

Tests:

- Automated compile checks listed in the final validation notes for this pass.
- Manual LAN streaming duration tests were not run in this coding session.

## 0.5 - 2026-06-21

- Removed the legacy WebSocket binary audio fallback and AudioWorklet browser player.
- Made WebRTC Opus the only supported audio transport; WS remains only for SDP/ICE signaling.
- Removed legacy transport, packet format, sample-rate, packet-duration, and buffer-target controls.
- Changed the network worker to feed WebRTC in Opus-frame-sized chunks instead of large FIFO bursts.
- Added RTP attempts/sent/failure diagnostics and advanced RTP timestamps only for audio emitted to an open track.
- Restored normal Release build behavior by removing the temporary safe `/MP1`, `/Od`, and `/Ob0` plugin compile flags.
- Added normal Visual Studio 2026 CMake presets and updated the release build script to use normal Release presets.
- Updated the plugin About panel version to 0.5 and the visible copyright name to Aras Pigeon.

## 0.4 - 2026-06-20

- Changed the embedded LAN server to HTTP/WS.
- Removed the active build dependency on local OpenSSL headers, libraries, executables, generated certificates, and private keys.
- Switched libdatachannel's WebRTC crypto backend to Mbed TLS 3.6.6 LTS.
- Added AGPL-3.0-only licensing and `THIRD_PARTY_NOTICES.md`.
- Updated the plugin About panel version from 0.3 to 0.4.

## 0.3 - 2026-06-20

- Added WebRTC Opus browser playback as the recommended transport.
- Included an early WebSocket audio fallback that was removed in 0.5.
- Added browser and plugin diagnostics for WebRTC state, packet counts, jitter, sender stats, and queue state.
- Fixed WebRTC SDP/media pairing by using the browser offer's audio MID and Opus payload type.
- Added the root `PGStream.vst3` distributable bundle.
