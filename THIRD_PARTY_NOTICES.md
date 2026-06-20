# Third-Party Notices

PGStream is licensed under AGPL-3.0-only. The following source dependencies are fetched into `external/` by `scripts/bootstrap_windows.ps1` and are used for the Windows VST3 build.

## JUCE

- Source: `external/JUCE`
- Upstream: `https://github.com/juce-framework/JUCE.git`
- Version: `8.0.13`
- Pinned commit: `7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2`
- License: JUCE modules are dual-licensed under AGPLv3 or a commercial JUCE license. PGStream uses the AGPLv3 option. See `external/JUCE/LICENSE.md`.

## Steinberg VST3 SDK Files Included by JUCE

- Source: `external/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK`
- License: MIT, as provided by the bundled `LICENSE.txt` files in that tree.
- Note: PGStream does not vendor a separate Steinberg SDK checkout; the VST3 support used by this build comes through JUCE.

## CivetWeb

- Source: `external/civetweb`
- Upstream: `https://github.com/civetweb/civetweb.git`
- Version: `v1.16`
- Pinned commit: `d7ba35bbb649209c66e582d5a0244ba988a15159`
- License: MIT. See `external/civetweb/LICENSE.md`.

## Mbed TLS

- Source: `external/mbedtls`
- Upstream: `https://github.com/Mbed-TLS/mbedtls.git`
- Version: `mbedtls-3.6.6`
- Pinned commit: `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`
- License: Apache-2.0 OR GPL-2.0-or-later; PGStream uses Apache-2.0. See `external/mbedtls/LICENSE`.
- Note: Mbed TLS is used as the libdatachannel DTLS/TLS backend.

## libdatachannel

- Source: `external/libdatachannel`
- Upstream: `https://github.com/paullouisageneau/libdatachannel.git`
- Pinned commit: `a542d8703bfab42a5533852e18d6d1879e01080a`
- License: MPL-2.0. See `external/libdatachannel/LICENSE`.

Bundled libdatachannel dependencies used by the static build:

- `deps/json`: MIT, nlohmann/json. See `external/libdatachannel/deps/json/LICENSE.MIT`.
- `deps/libjuice`: MPL-2.0. See `external/libdatachannel/deps/libjuice/LICENSE`.
- `deps/libsrtp`: BSD-style license. See `external/libdatachannel/deps/libsrtp/LICENSE`.
- `deps/plog`: MIT. See `external/libdatachannel/deps/plog/LICENSE`.
- `deps/usrsctp`: BSD-style license. See `external/libdatachannel/deps/usrsctp/LICENSE.md`.

## Opus

- Source: `external/opus`
- Upstream: `https://github.com/xiph/opus.git`
- Pinned commit: `3da9f7a6db1c05c3996cb363a9d1931a978bf1be`
- License: BSD-style Opus license. See `external/opus/COPYING` and `external/opus/LICENSE_PLEASE_READ.txt`.

## Local Assets and QR Code

- `assets/logo.png`, `assets/pgs.png`, and the embedded web UI are project assets.
- The QR code renderer in `src/PluginEditor.cpp` is project source code and is not a vendored third-party QR library.

## Binary Build Policy

The repository tracks the root `PGStream.vst3` distributable bundle. Generated build folders, duplicate `dist` outputs, downloaded dependency checkouts, private keys, certificates, and external binary SDK blobs are intentionally not tracked.
