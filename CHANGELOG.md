# Changelog

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
