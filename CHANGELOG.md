# Changelog

## 0.4 - 2026-06-20

- Changed the embedded LAN server to HTTP/WS.
- Removed the active build dependency on local OpenSSL headers, libraries, executables, generated certificates, and private keys.
- Switched libdatachannel's WebRTC crypto backend to Mbed TLS 3.6.6 LTS.
- Added AGPL-3.0-only licensing and `THIRD_PARTY_NOTICES.md`.
- Updated the plugin About panel version from 0.3 to 0.4.

## 0.3 - 2026-06-20

- Added WebRTC Opus browser playback as the recommended transport.
- Kept WebSocket legacy playback as a fallback.
- Added browser and plugin diagnostics for WebRTC state, packet counts, jitter, sender stats, and queue state.
- Fixed WebRTC SDP/media pairing by using the browser offer's audio MID and Opus payload type.
- Added the root `PGStream.vst3` distributable bundle.
