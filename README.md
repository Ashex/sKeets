# sKeets

![sKeets Logo](src/img/skeets_splash_asset "sKeets for Kobo")

A [Bluesky](https://bsky.app) (ATProto) social client for the **Kobo Clara Colour** e-ink reader, written in C++23.

## Features

- Login via `login.txt` (see Getting Started below)
- Scrollable home timeline feed with swipe gestures
- Post thread / comments view with recursive reply display
- Optional profile image and post image display (off by default)
- Quote post and external link rendering
- Repost attribution display
- Settings persistence via INI config file
- Optimised for 1448×1072 e-ink display with MXCFB partial refresh

## Getting Started

### Authentication

sKeets uses a `login.txt` file for initial authentication:

1. Connect your Kobo device via USB
2. Create the file `.adds/sKeets/login.txt` with the following content:
   ```
   handle=yourhandle.bsky.social
   password=your-app-password
   ```
   Optional fields:
   ```
   pds_url=https://bsky.social
   appview=https://api.bsky.app
   ```
3. Get an App Password from bsky.app → Settings → App Passwords
4. Eject USB and restart sKeets

After successful login, the `login.txt` file is automatically deleted and your session is saved to `config.ini`. On subsequent launches, sKeets uses the saved session tokens and refreshes them automatically before they expire.

## Project Structure

```
sKeets/
├── CMakeLists.txt              Build system (C++23, Qt6, CMake 3.16+)
├── run.sh                      Main Kobo launcher
├── launch/                     NickelMenu-friendly launcher wrappers
├── scripts/wifi/               Device Wi-Fi helper scripts
├── toolchain/
│   └── arm-kobo-linux-gnueabihf.cmake  ARM cross-compilation toolchain
├── src/
│   ├── kobo/                   Main app entry points and feed/thread logic
│   │   ├── main.cpp
│   │   ├── bootstrap.h/cpp
│   │   ├── actions.h/cpp
│   │   ├── feed.h/cpp
│   │   ├── thread.h/cpp
│   │   ├── diag_main.cpp
│   │   └── tool_main.cpp
│   ├── platform/               Kobo hardware abstractions
│   │   ├── device.h/cpp
│   │   ├── framebuffer.h/cpp
│   │   ├── input.h/cpp
│   │   ├── network.h/cpp
│   │   └── power.h/cpp
│   ├── atproto/                ATProto / Bluesky API layer
│   ├── ui/                     Shared framebuffer/font helpers
│   │   ├── fb.h/cpp
│   │   └── font.h/cpp
│   └── util/                   Config, image, path, and string utilities
```

## Prerequisites

### Host (build machine)

- CMake ≥ 3.16
- ARM cross-toolchain: `arm-linux-gnueabihf-gcc`
  - Debian/Ubuntu: `sudo apt install gcc-arm-linux-gnueabihf`
  - Or download from [Linaro](https://releases.linaro.org/components/toolchain/binaries/)
- Qt6 for ARM (or in your sysroot)

### Optional: Kobo sysroot

For a fully linked binary matching the device, set `KOBO_SYSROOT` to a Kobo
rootfs image or an extracted `KoboRoot.tgz`:

```sh
export KOBO_SYSROOT=/opt/kobo-sysroot
```

A pre-built sysroot can be obtained from the
[KoboTolinoFindings](https://github.com/notmarek/KoboTolinoFindings) project.

## Building

### Docker build (recommended)

Use `docker-build.sh` for a repeatable cross-compilation inside Docker. Build
artifacts land in `build-kobo/`. To remove root-owned files left behind by
Docker bind mounts, use `sudo rm -rf build-kobo`.

```sh
make build
# Produces: build-kobo/KoboRoot.tgz

./docker-build.sh
# Produces: build-kobo/KoboRoot.tgz
```

The package currently boots a diagnostics entrypoint by default via
`/mnt/onboard/.adds/sKeets/run.sh`. It also ships app-owned
Wi-Fi lifecycle scripts under `/mnt/onboard/.adds/sKeets/scripts/wifi/`.
Use `/mnt/onboard/.adds/sKeets/run.sh app` to launch the first
app shell after Nickel is stopped.
Use `/mnt/onboard/.adds/sKeets/run.sh fb-diag` to validate the
framebuffer and refresh path with Nickel stopped.
The package also installs these launcher helpers:
- `/mnt/onboard/.adds/sKeets/launch-app.sh`
- `/mnt/onboard/.adds/sKeets/launch-diag.sh`
- `/mnt/onboard/.adds/sKeets/launch-fb-diag.sh`
- `/mnt/onboard/.adds/sKeets/launch-input-diag.sh`
- `/mnt/onboard/.adds/sKeets/launch-power-diag.sh`
- `/mnt/onboard/.adds/sKeets/launch-network-diag.sh`
- `/mnt/onboard/.adds/sKeets/launch-phase2-diag.sh`

### Native (for development / testing on ARM Linux)

```sh
mkdir build && cd build
cmake .. -GNinja
ninja -j$(nproc)
```

### Cross-compile for Kobo Clara Colour

```sh
mkdir build-arm && cd build-arm
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=../toolchain/arm-kobo-linux-gnueabihf.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -GNinja
ninja -j$(nproc)
```

### Create a KoboRoot package

```sh
ninja kobo-package
# Produces: build-arm/KoboRoot.tgz
```

## Installation

1. Copy `KoboRoot.tgz` to `/mnt/onboard/.kobo/KoboRoot.tgz` on the mounted Kobo storage (via USB).
2. Safely eject the Kobo. The firmware will extract `KoboRoot.tgz` automatically on the next reboot.
3. The binary lands at `/mnt/onboard/.adds/sKeets/sKeets`.
4. Add a launcher entry via [NickelMenu](https://github.com/pgaskin/NickelMenu) or [KFMon](https://github.com/NiLuJe/kfmon).

### NickelMenu entries (`/mnt/onboard/.adds/nm/sKeets`)

```
menu_item :main  :sKeets sKeets      :cmd_spawn  :quiet:/mnt/onboard/.adds/sKeets/launch-app.sh
menu_item :main  :sKeets Diag     :cmd_spawn  :quiet:/mnt/onboard/.adds/sKeets/launch-diag.sh
menu_item :main  :sKeets FB Diag  :cmd_spawn  :quiet:/mnt/onboard/.adds/sKeets/launch-fb-diag.sh
menu_item :main  :sKeets Input Diag  :cmd_spawn  :quiet:/mnt/onboard/.adds/sKeets/launch-input-diag.sh
menu_item :main  :sKeets Power Diag  :cmd_spawn  :quiet:/mnt/onboard/.adds/sKeets/launch-power-diag.sh
menu_item :main  :sKeets Network Diag  :cmd_spawn  :quiet:/mnt/onboard/.adds/sKeets/launch-network-diag.sh
menu_item :main  :sKeets Phase2 Diag  :cmd_spawn  :quiet:/mnt/onboard/.adds/sKeets/launch-phase2-diag.sh
```

The packaged `run.sh` wrapper kills Nickel and its companion processes so sKeets can
take over the framebuffer, feeds the hardware watchdog, and reboots the device
on exit to bring Nickel back. The `launch-*.sh` helpers are the intended menu
entrypoints. The `:quiet` flag suppresses the PID notification popup. Logs are
written to `/mnt/onboard/.adds/sKeets/sKeets.log`.

`sKeets App` stops Nickel, opens the framebuffer and input
paths, reads local credentials from `/mnt/onboard/.adds/sKeets/login.txt`,
persists local session state in `/mnt/onboard/.adds/sKeets/config.ini`,
renders the bootstrap/auth shell, and lets you exit cleanly via the on-screen
Exit button or the hardware power key.
`sKeets Diag` only runs the environment and package diagnostics.
`sKeets FB Diag` stops Nickel first, opens the framebuffer path,
draws a simple grayscale test pattern, and logs refresh capability details.
`sKeets Input Diag` stops Nickel first, grabs the selected input devices,
logs the touch device selection, and listens briefly for raw touch transitions.
`sKeets Power Diag` keeps Nickel running and reports battery state,
charging state, standby support, and whether suspend is currently safe.
`sKeets Network Diag` keeps Nickel running and reports radio presence,
link state, assigned addresses, default-route availability, DNS resolution, and overall online state.
`sKeets Phase2 Diag` stops Nickel once and runs the full Phase 2 diagnostic pass
across framebuffer, input, power, and network in a single launch.

### KFMon entry (with app icon)

[KFMon](https://github.com/NiLuJe/kfmon) lets you launch sKeets by tapping a
cover image in your library. Place a PNG on the Kobo and create a config file:

1. Copy your icon image to `/mnt/onboard/sKeets.png`.
2. Create `/mnt/onboard/.adds/kfmon/config/sKeets.ini`:

```ini
[watch]
filename = /mnt/onboard/sKeets.png
action = /mnt/onboard/.adds/sKeets/launch-app.sh
label = sKeets
hidden = false
```

3. Reboot. The PNG appears as a book cover in your library — tapping it launches sKeets.

You can use both launchers simultaneously (NickelMenu for the menu shortcut, KFMon for the visual icon).

## Configuration

Config file: `/mnt/onboard/.adds/sKeets/config.ini`

```ini
handle=you.bsky.social
access_jwt=<stored token>
refresh_jwt=<stored token>
did=did:plc:...
pds_url=https://morel.us-east.host.bsky.network
images_enabled=false
```

All fields except `images_enabled` are written automatically by the login flow
(see below). Tokens are stored in plain text. Use a dedicated
[app password](https://bsky.app/settings/app-passwords) rather than your main
account password. Initial sign-in currently uses `login.txt`; `config.ini` is
generated and updated by the app after a successful login.

For development and future plugin migration work, the storage root can be
overridden with the `SKEETS_DATA_DIR` environment variable. If unset, the app
defaults to `/mnt/onboard/.adds/sKeets`.

### Quick Setup (pre-fill from computer)

Typing on the Kobo's touch keyboard is slow. You can skip it entirely by
pre-filling your credentials from a computer with `login.txt`:

1. Connect the Kobo via USB and open the mounted drive.
2. Create or edit `.adds/sKeets/login.txt` with a text editor:

   ```ini
   handle=you.bsky.social
   password=xxxx-xxxx-xxxx-xxxx
   pds_url=
   appview=https://api.bsky.app
   ```

   Leave `pds_url` blank for Bluesky accounts. Set it only for a self-hosted or
   third-party PDS. `appview` is optional and can usually be left at the
   default.
3. Safely eject the Kobo and launch sKeets.
4. The app reads the pre-filled credentials, logs in automatically, and
   **removes `login.txt` after a successful read**.

If the auto-login fails (wrong password, network error, etc.) the login screen
is shown with the handle already filled in so you only need to re-enter the
password.

### Login Flow & PDS Resolution

1. The login screen presents three fields: **Handle or email**, **App password**,
   and an optional **PDS URL**.
2. If the PDS URL field is left blank the client authenticates against the
   default Bluesky Entryway (`https://bsky.social`). Users on a self-hosted or
   third-party PDS should enter their PDS URL here (e.g.
   `https://pds.example.com`).
3. On successful `com.atproto.server.createSession`, the response includes a
   DID document. The client calls the SDK's `getPDS()` to extract the
   `#atproto_pds` service endpoint from that document.
4. If a PDS endpoint is found in the DID document, the XRPC client is
   re-pointed to that host so all subsequent API calls go directly to the
   user's actual PDS instead of routing through the Entryway.
5. The resolved `pds_url`, session tokens, DID, and handle are persisted to
   `config.ini`.
6. On the next launch the app reads `pds_url` from the config, constructs the
   XRPC client with that host, and attempts `resumeSession`. If the session is
   still valid the user skips the login screen entirely.

## Display

The Kobo Clara Colour uses a 1448×1072 Kaleido 3 colour e-ink display
(colour filter array). The app renders in 16-bit RGB565. Two refresh modes are
used:

| Mode      | MXCFB constant | Usage                  |
|-----------|---------------|------------------------|
| GC16      | `0x2`          | Full screen redraws     |
| DU        | `0x1`          | Partial / scroll updates|

## Architecture

```
main.c
  └── app_run()            ← event loop
        ├── input_poll()   ← evdev touch / key events
        └── *_view_handle() ← dispatch to active view
              └── *_view_draw() ← render to framebuffer
                    └── fb_refresh_*() ← MXCFB ioctl
```

The ATProto layer is synchronous (blocking HTTP). On slow networks or large
feeds this may cause brief pauses; a future improvement would be to offload
network calls to a background thread.

## Reference Projects

The following open-source projects were invaluable references during development. Their codebases informed sKeets' approach to e-ink rendering, touch input handling, device quirk management, and Kobo platform integration.

| Project | Description | License |
|---------|-------------|---------|
| [KOReader](https://github.com/koreader/koreader) | Document reader for e-ink devices — reference for per-device touch transforms, EPDC waveform selection, suspend/resume, and input handling | AGPL-3.0 |
| [KOReader-base](https://github.com/koreader/koreader-base) | KOReader's C/Lua platform layer — reference for framebuffer abstraction, mxcfb/MTK/sunxi ioctl patterns, and blitbuffer architecture | AGPL-3.0 |
| [NickelMenu](https://github.com/pgaskin/NickelMenu) | Menu injection plugin for Kobo's Nickel reader — used as the primary app launcher via `cmd_spawn` | MIT, Copyright (c) 2020-2025 Patrick Gaskin |
| [NickelHook](https://github.com/pgaskin/NickelHook) | PLT/GOT hooking library for Nickel — NickelMenu's hook foundation and reference for Nickel plugin architecture | MIT, Copyright (c) 2020-2025 Patrick Gaskin |
| [Kobo-Reader](https://github.com/kobolabs/Kobo-Reader) | Kobo's open-source build tree — reference for cross-compilation toolchain setup and QWidget plugin interface examples | No explicit license (Kobo Labs) |
| [kobo-anki](https://github.com/tent4kel/kobo-anki) | Anki flashcard app for Kobo using FBInk — reference for FBInk integration patterns on Kobo | MIT, Copyright (c) 2025 tent4kel |

## License

GPLv3 — see individual source files for copyright notices.

### Third-party components

- **stb_image**: MIT/Public Domain, Copyright (c) 2017 Sean Barrett
- **[libatproto](https://github.com/mfnboer/atproto)**: C++ ATProto SDK by mfnboer (pinned commit, locally patched for Kobo cross-compilation)
- **[FBInk](https://github.com/NiLuJe/FBInk)**: E-ink framebuffer library by NiLuJe
