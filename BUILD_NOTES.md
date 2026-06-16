# Build Notes

PGStream is a native Windows x64 VST3 built with JUCE, CMake, CivetWeb, and OpenSSL. The project is designed so the DAW audio path remains transparent while browser streaming runs on a separate non-realtime network worker.

## Source Dependencies

The repository does not vendor JUCE, CivetWeb, CMake, generated certificates, build folders, or duplicate `dist` artefacts. The root `PGStream.vst3` bundle is tracked as the distributable VST3 artefact. Run the bootstrap script to fetch pinned dependencies and generate local development certificates:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bootstrap_windows.ps1
```

Pinned dependency versions:

- JUCE: `8.0.13`
- CivetWeb: `v1.16`
- CMake: `4.3.3` project-local fallback when the system CMake is too old

## TLS Strategy

PGStream serves the browser UI over HTTPS/WSS because browser AudioWorklet support on phones and LAN clients generally requires a secure context. The development certificate and private key are generated locally under `assets\certs` by `scripts\generate_dev_cert.ps1` and embedded into the plugin binary at build time.

Generated certificate files are intentionally ignored by git and must not be committed:

- `assets\certs\dev-cert.pem`
- `assets\certs\dev-key.pem`
- `assets\certs\dev-cert.cnf`

The self-signed certificate is expected to appear as not trusted until accepted by the browser or installed into a device trust store.

## Safe Windows Build

Use the safe release build script:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_release_windows.ps1
```

The safe presets disable interprocedural optimization and build with one parallel job to reduce peak RAM and disk pressure on Windows machines. The project does not require LTO for correctness.

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
- OpenSSL is statically linked, with no `libssl` or `libcrypto` DLL dependency

`scripts\audit_windows_daw_visibility.ps1` is optional. It discovers validators, pluginval, and common DAWs, but may recursively scan `C:\Program Files` and `C:\Program Files (x86)`.

## Streaming Behavior

Normal server mode packetizes audio on the network worker into complete fixed-duration WSS frames. The default packet duration is 20 ms, with optional 40 ms and 60 ms modes. The experimental **extr666me** mode sends complete 5 ms packets and is not the default.

FIFO polling is not treated as an underrun when there are simply not enough frames for a complete packet yet. Server FIFO underruns are reserved for real source starvation after the worker has waited/backed off. If keep-alive is enabled during real idle, full-size silence packets are sent at the selected packet duration.

Browser buffer targets are 20, 40, 60, 100, 250, 500, and 1000 ms. The AudioWorklet starts playback after the selected target is buffered, resyncs back to that target after starvation, and trims excess queued audio so latency does not silently drift toward seconds unless a large target is selected.

## 2026-06-16 Polish and Buffer Fix Pass

Changed:

- Compacted the plugin editor header from a 780 px-wide layout to a 600 px content-driven layout. The title/info area now sits close to the QR code and `pgs.png` logo with fixed, clean spacing instead of a large flexible gap.
- Added a small circular `i` button near the title and an in-editor About panel with a circular `X` close button.
- Embedded and displayed the existing `assets/logo.png` image at the top of the About panel. `assets/pgs.png` remains unchanged and is still used for the normal editor/browser logo.
- Rebuilt the buffer target choices around one ordered source of truth: 20, 40, 60, 100, 250, 500, and 1000 ms. The GUI choice, stored parameter index, `StreamConfig`, `/info` metadata, browser setup, and AudioWorklet target now resolve to the same millisecond value.
- Adjusted the AudioWorklet queue logic so normal low-buffer variation does not trigger periodic resync. Resync now represents true queue starvation, while excess queued audio is trimmed back toward the selected target instead of being allowed to drift or being dropped below target.

Deliberately not changed:

- DAW audio pass-through and `processBlock`.
- The network worker packet accumulator, WSS frame format, and current packetization behavior.
- HTTPS/WSS, CivetWeb, QR generation, LAN IP selection, server start/stop behavior, and Nerd diagnostics.
- VST3 identity, bundle naming, build system architecture, and install/copy behavior outside the project.

Preserved behavior:

- Server remains disabled by default for DAW scan safety.
- Normal mode still sends complete aggregated packets, with 20 ms as the default packet duration.
- Browser playback remains plain HTML/CSS/JS plus AudioWorklet, without Node, npm, React, Electron, WebRTC, or helper daemons.

Files added:

- No new source files were created by this pass.
- The pre-existing local `assets/logo.png` asset is now referenced by the build and must be tracked with the project for clean rebuilds on other machines.

Files removed:

- None.

Validation performed:

- Confirmed `processBlock` and the network packet accumulator were not edited.
- Confirmed `assets/logo.png` is included in `juce_add_binary_data` and loaded by the About panel through `PGStreamBinaryData`.
- Confirmed the buffer target flow is `AudioParameterChoice` index -> `bufferTargetMsForIndex()` -> `StreamConfig.bufferTargetMs` -> `/info` metadata -> browser `configure` message -> AudioWorklet target frames.
- Ran `scripts\bootstrap_windows.ps1` with GitHub Desktop Git added to `PATH`. It downloaded project-local CMake 4.3.3 under `tools\`, then stopped because no Visual Studio C++ toolchain was found.
- Ran `scripts\build_release_windows.ps1` with GitHub Desktop Git added to `PATH`; it stopped while generating certificates because OpenSSL was not found in this session.
- Ran `scripts\verify_artifact_windows.ps1`; it failed because `dist\PGStream.vst3` and `PGStream.vst3` do not exist yet.
- Build and DAW visibility audit were not completed on this machine because `cl.exe`, `vswhere.exe`, and `winget` were not available in this session.

## LAN URL and QR

The server binds broadly for LAN reachability, while the displayed URL and QR code use a ranked local IPv4 selection:

1. Active private Wi-Fi adapter with IPv4 default gateway
2. Active private Ethernet adapter with IPv4 default gateway
3. Other active private adapter with gateway
4. Other active private adapter
5. Other non-loopback IPv4
6. `169.254.x.x` link-local only as a last resort

Virtual/host-only adapters such as VirtualBox, VMware, Hyper-V/vEthernet, Docker, WSL, TAP/TUN/VPN, Loopback, and Host-Only are downranked. The QR code encodes the full HTTPS URL shown in the plugin UI.

## Realtime Safety

The audio thread does not perform network I/O, filesystem I/O, blocking waits, sleeps, or resampling. It only passes audio through and writes the tapped stereo copy into the preallocated FIFO when streaming is enabled.
