# Changelog

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
