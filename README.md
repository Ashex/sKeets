# sKeets

A [Bluesky](https://bsky.app) (ATProto) social client for the **Kobo Clara Colour** e-ink reader, written in C++23.

## Features

- Login with Bluesky handle/email and app password
- Scrollable home timeline feed
- Post thread / comments view
- Compose new posts and replies
- Optional profile image and post image display (off by default)
- Quote post rendering with visual indent
- Settings persistence via INI config file
- Optimised for 1448×1072 e-ink display with MXCFB partial refresh

## Project Structure

```
apps/sKeets/
├── CMakeLists.txt              Build system (C++23, Qt6, CMake 3.16+)
├── toolchain/
│   └── arm-kobo-linux-gnueabihf.cmake  ARM cross-compilation toolchain
├── src/
│   ├── main.cpp                  Entry point
│   ├── app.h / app.cpp           App state machine & event loop
│   ├── atproto/                ATProto / Bluesky API layer
│   │   ├── atproto.h           Shared C++ data structures (Bsky namespace)
│   │   ├── atproto_client.h/cpp  ATProto SDK wrapper (mfnboer/atproto)
│   ├── ui/                     UI / rendering layer
│   │   ├── fb.h/c              Framebuffer + MXCFB e-ink refresh
│   │   ├── font.h/c            Embedded 8×16 bitmap font
│   │   ├── input.h/c           Linux evdev touch & key input
│   │   ├── views.h             Shared view enum & layout constants
│   │   ├── login_view.h/c
│   │   ├── feed_view.h/c
│   │   ├── thread_view.h/c
│   │   ├── compose_view.h/c
│   │   └── settings_view.h/c
│   └── util/
│       ├── str.h/c             String utilities
│       ├── config.h/c          INI-style config persistence
│       └── image.h/c           Image download & decode (stb_image)
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

### Native (for development / testing on ARM Linux)

```sh
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Cross-compile for Kobo Clara Colour

```sh
mkdir build-arm && cd build-arm
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=../toolchain/arm-kobo-linux-gnueabihf.cmake \
  -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Create a KoboRoot package

```sh
make kobo-package
# Produces: build-arm/sKeets-koboroot.tgz
```

## Installation

1. Copy `sKeets-koboroot.tgz` to the root of the Kobo's SD card (via USB).
2. Safely eject the Kobo. The firmware will extract `KoboRoot.tgz` automatically on next boot.
3. The binary lands at `/mnt/onboard/.adds/sKeets/sKeets`.
4. Add a launcher entry via [NickelMenu](https://github.com/pgaskin/NickelMenu) or [KFMon](https://github.com/NiLuJe/kfmon).

### NickelMenu entry (`/mnt/onboard/.adds/nm/sKeets`)

```
menu_item :main  :sKeets  :cmd_spawn  :/mnt/onboard/.adds/sKeets/sKeets
```

NickelMenu adds a text-only entry to the main menu — it does not support custom icons.

### KFMon entry (with app icon)

[KFMon](https://github.com/NiLuJe/kfmon) lets you launch sKeets by tapping a
cover image in your library. Place a PNG on the Kobo and create a config file:

1. Copy your icon image to `/mnt/onboard/sKeets.png`.
2. Create `/mnt/onboard/.adds/kfmon/config/sKeets.ini`:

```ini
[watch]
filename = /mnt/onboard/sKeets.png
action = /mnt/onboard/.adds/sKeets/sKeets
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
account password.

### Quick Setup (pre-fill from computer)

Typing on the Kobo's touch keyboard is slow. You can skip it entirely by
pre-filling your credentials from a computer:

1. Connect the Kobo via USB and open the mounted drive.
2. Create or edit `.adds/sKeets/config.ini` with a text editor:

   ```ini
   handle=you.bsky.social
   app_password=xxxx-xxxx-xxxx-xxxx
   pds_url=
   ```

   Leave `pds_url` blank for Bluesky accounts. Set it only for a self-hosted or
   third-party PDS.
3. Safely eject the Kobo and launch sKeets.
4. The app reads the pre-filled credentials, logs in automatically, and
   **removes `app_password` from the file** — it is never kept on disk after
   the first launch.

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

## License

GPLv3 — see individual source files for copyright notices.  

stb_image: MIT/Public Domain, Copyright (c) 2017 Sean Barrett.
