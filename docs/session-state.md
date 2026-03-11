# sKeets Rewrite — Session State & Plan

> Written for IDE restart continuity. Last updated: 2026-03-11.
> Memory files: `/memories/repo/kobo-skeets-notes.md`, `/memories/repo/skeets-codebase-overview.md`, `/memories/repo/kobo-tls-fixes.md`

---

## Current State: Authentication COMPLETE, Feed Bootstrap NEXT

The rewrite has successfully completed its first on-device createSession. TLS,
DNS, and auth all work. The app is a "bootstrap shell" — a diagnostic dashboard
with 4 cards (Auth, Device, Power, Connectivity) and Recheck/Exit buttons. The
next milestone is **feed bootstrap**: calling `getTimeline()` and displaying
posts on the e-ink screen.

### Latest working build

- **build_timestamp**: `2026-03-11T21:30:33Z`
- **Package**: `build-kobo/KoboRoot.tgz`
- **Build command**: `NINJA_PACKAGE_TARGET=kobo-package-rewrite ./docker-build.sh`

### Device under test

- **Kobo Clara Colour** — product_id=393, codename=spaColour, platform=mt8113t-ntx
- **Touch protocol**: snow (cyttsp5_mt) — set via `SKEETS_REWRITE_TOUCH_PROTOCOL=snow`
- **Display**: 1448×1072 (landscape-native NTX quirk, FBInk auto-rotates)
- **Firmware**: N367490221161,4.9.77,4.44.23552
- **Filesystem**: `/mnt/onboard` is VFAT — no symlinks possible

### User's Bluesky account

- **Handle**: nihilist.cloud
- **PDS**: https://nihilist.cloud (self-hosted)
- **DNS**: Fritz.box router at 192.168.178.1

---

## Phase Map (from docs/rewrite-plan.md)

| Phase | Description | Status |
|-------|-------------|--------|
| **Phase 0** | Fix coordinate misalignment (hardcoded dims) | **DEFERRED** — rewrite uses runtime FBInk dims already |
| **Phase 1** | Scaffolding (remove compose/login views, add auth_view) | **PARTIALLY DONE** — rewrite has its own file structure |
| **Phase 2** | Credential file login (login.txt flow) | **DONE** ✅ |
| **Phase 3** | Like and repost support | Not started |
| **Phase 3.5** | Settings view: dual image toggles | Not started |
| **Phase 4** | Input improvements (swipe, long-press) | Not started |
| **Phase 5** | Framebuffer & reliability improvements | Not started |
| **Phase 5.5** | Bug fixes (JWT refresh, thread recursion, etc.) | Not started |
| **Phase 6** | Cleanup (README, docs, stale comments) | Not started |

### What "Phase 2 DONE" means concretely

1. `login.txt` is read from `/mnt/onboard/.adds/sKeets-rewrite/login.txt`
2. `createSession()` succeeds against a self-hosted PDS (nihilist.cloud)
3. Session tokens saved to `config.ini` (handle, access_jwt, refresh_jwt, did, pds_url, appview_url)
4. `login.txt` securely deleted after reading (whether login succeeds or fails)
5. On relaunch, `resumeSession()` reads from `config.ini` — **not yet tested on-device** but code is in place
6. The auth bootstrap shell displays confirmation on successful login

---

## TLS Fix History (critical lessons)

Detailed in `/memories/repo/kobo-tls-fixes.md`. Summary:

### Problem 1: OpenSSL version mismatch
- Kobo firmware ships OpenSSL 1.0.x in system paths
- Qt TLS plugin (`libqopensslbackend.so`) uses `dlopen()` at runtime, finds system 1.0.x
- Neither `LD_LIBRARY_PATH` nor `LD_PRELOAD` work (likely ld-linux loader + VFAT issue)
- **Fix**: Early `dlopen()` with absolute paths + `RTLD_NOW | RTLD_GLOBAL` in `main()` before QCoreApplication

### Problem 2: VFAT drops symlinks silently
- `libssl.so → libssl.so.3` symlinks in tar silently dropped when extracted to VFAT
- **Fix**: Real file copies in `cmake/package-runtime-libs.cmake` (`file(COPY_FILE ...)`)

### Problem 3: CA certificate verification
- Qt on embedded Linux does NOT honor `SSL_CERT_FILE` env var
- **Fix**: Explicit `QSslCertificate::fromDevice()` + `QSslConfiguration::setDefaultConfiguration()` after QCoreApplication init

### Problem 4: DNS resolution (transient)
- "Host nihilist.cloud not found" — resolved on next run
- `resolv.conf` was properly populated by dhcpcd (`nameserver 192.168.178.1`)
- Likely timing issue after killing Nickel

---

## Files Modified This Session

### `src/rewrite/app_main.cpp`
The main entry point for the rewrite app. Changes this session:
- **dlopen OpenSSL preload** (lines ~358-415): Force-loads bundled OpenSSL 3.x with `RTLD_NOW | RTLD_GLOBAL` before Qt init. Includes verbose diagnostic probes (dlsym, RTLD_NOLOAD, version_num checks for each lib name Qt might try).
- **CA cert loading** (lines ~420-445): After QCoreApplication init, loads bundled CA certs from `<rewrite_dir>/ssl/certs/ca-certificates.crt` via `QSslCertificate::fromDevice()` and sets as default SSL config.
- **Includes added**: `<QFile>`, `<QSslCertificate>`, `<QSslConfiguration>`, `<dlfcn.h>`, `<fstream>`

### `src/rewrite/bootstrap.cpp`
The auth bootstrap logic. Changes this session:
- **resolv.conf diagnostic logging** (lines ~170-180): Before createSession, reads and logs `/etc/resolv.conf` to stderr for debugging DNS issues
- **Include added**: `<fstream>`

### `cmake/package-runtime-libs.cmake`
Library packaging for KoboRoot archive. Changes this session:
- Replaced all `file(CREATE_LINK ... SYMBOLIC)` with `file(COPY_FILE ... ONLY_IF_DIFFERENT)` for libssl.so and libcrypto.so in both `DEST_LIB_DIR` and `DEST_APP_DIR/lib`

### `CMakeLists.txt`
- `sKeets_rewrite` target now links `dl` (for dlopen/dlsym)

### `Dockerfile.cross`
- Added `RUN cat /etc/ssl/certs/*.pem > /etc/ssl/certs/ca-certificates.crt` to rebuild CA bundle (this turned out to be unnecessary — the bundle was already complete, but it's harmless)

---

## Rewrite Architecture (current)

The rewrite is **not** modifying the v1 source tree. It lives in `src/rewrite/` with its own file structure:

```
src/rewrite/
├── app_main.cpp              Entry point + OpenSSL preload + CA loading + UI event loop
├── bootstrap.h/cpp           Auth flow: login.txt → createSession / config.ini → resumeSession
├── tool_main.cpp             Diagnostic tool entry point
├── diag_main.cpp             Hardware diagnostics entry point
└── platform/
    ├── device.h/cpp          Device detection (model, codename, platform, touch protocol)
    ├── framebuffer.h/cpp     FBInk-based framebuffer abstraction
    ├── input.h/cpp           Touch input via evdev (supports snow protocol for Clara Colour)
    ├── network.h/cpp         Network state probing (carrier, DNS, addresses)
    └── power.h/cpp           Battery/charging state probing
```

### Key types in the rewrite

- `rewrite_framebuffer_t` — FBInk framebuffer context with screen dimensions and font metrics
- `rewrite_input_t` — evdev input context with coordinate normalization
- `rewrite_bootstrap_result_t` — Auth bootstrap outcome (state enum + session + messages)
- `rewrite_device_info_t` — Device hardware info
- `rewrite_power_info_t` / `rewrite_network_info_t` — Runtime hardware state
- `rewrite_app_t` — Main app state (all of the above + UI state)

### Build targets

- `sKeets` — v1 app (original)
- `sKeets_rewrite` — v2 rewrite app (auth bootstrap shell)
- `sKeets_diag` — Hardware diagnostic tool
- `kobo-package-rewrite` — Packages rewrite app + libs + fonts into KoboRoot.tgz

### External dependencies

- **libatproto** (mfnboer/atproto, pinned SHA 2a1019b) — C++ ATProto SDK
- **Qt 6.5.2** — Core, Network, Qml (cross-compiled for ARM)
- **FBInk** — E-ink framebuffer library (built as ExternalProject)
- **OpenSSL 3.0.13** — bundled in package for TLS
- **stb_image** — Image decoding (not yet used in rewrite)

---

## Immediate Next Steps

### 1. Session Resume Test (quick validation)
- Relaunch the app **without** `login.txt` on the device
- The app should detect `config.ini`, call `resumeSession()`, and show "Saved session restored"
- This validates the `load_saved_session()` → `resumeSession()` → `session_restored` path in `bootstrap.cpp`

### 2. Feed Bootstrap (new milestone)
This is the next major feature. Required work:

1. **Add `getTimeline()` call after auth succeeds**
   - In `bootstrap.cpp` or a new `feed.cpp`, call `client.getTimeline()` after successful auth
   - The ATProto SDK's `ATProto::Client::getTimeline()` returns feed items
   - Parse into a list of `Bsky::Post` structs (from existing `atproto/atproto.h`)

2. **Display posts on the bootstrap shell screen**
   - Replace or augment the 4-card dashboard with a scrollable post list
   - Each post: author handle, timestamp, content text (truncated)
   - Use `font_draw_wrapped()` for text rendering
   - No images yet — text-only feed display

3. **Wire up ATProto client with appview URL**
   - Feed queries go to `appview_url` (default `https://api.bsky.app`), not the PDS
   - The ATProto SDK's client needs to be configured with the appview endpoint for feed/thread queries

4. **Pagination cursor**
   - Store the cursor from `getTimeline()` response
   - Refresh button loads next page or refreshes feed

### 3. Cleanup (low priority, do after feed works)
- Remove verbose dlopen diagnostic probes from `app_main.cpp` (keep the core dlopen preload + CA loading)
- Remove resolv.conf logging from `bootstrap.cpp`

---

## ATProto SDK Reference

The pinned libatproto snapshot:
- `ATProto::Client` — low-level XRPC client
- `ATProto::PostMaster` — high-level post operations (like, repost, undo)
- Feed reasons exposed as `std::optional<std::variant<ReasonRepost::SharedPtr, ReasonPin::SharedPtr>>`
- `PostMaster::like/repost` require empty `viaUri` and `viaCid` arguments when unused
- Session management: `createSession()`, `resumeSession()`, `refreshSession()`
- Timeline: `getTimeline()` returns `AppBskyFeed::GetTimelineOutput`

### Wrapper pattern (synchronous via QEventLoop)

All SDK calls in the rewrite use synchronous wrappers:
```cpp
Bsky::AtprotoClient client(pds_url);
bool success = false;
std::string error;
client.createSession(handle, password,
    [&](const Bsky::Session& s) { success = true; session = s; },
    [&](const std::string& e) { error = e; });
// QEventLoop runs inside the client wrapper until callback fires
```

---

## Docker Build & Deploy Workflow

```bash
# Build KoboRoot.tgz for the rewrite target
NINJA_PACKAGE_TARGET=kobo-package-rewrite ./docker-build.sh

# The output is at build-kobo/KoboRoot.tgz
# Copy to Kobo's .kobo/ directory for auto-extraction on disconnect:
cp build-kobo/KoboRoot.tgz /media/$USER/KOBOeReader/.kobo/KoboRoot.tgz

# Or manually extract to test directory:
# The package extracts to /mnt/onboard/.adds/sKeets-rewrite/
```

### Environment variables on device

Set in `run-rewrite.sh` (launched via NickelMenu):
- `SKEETS_DATA_DIR` — data directory path
- `SKEETS_REWRITE_DIR` — rewrite app directory (default: `/mnt/onboard/.adds/sKeets-rewrite`)
- `SKEETS_REWRITE_TOUCH_PROTOCOL=snow` — for Clara Colour touch input
- `LD_LIBRARY_PATH` — includes bundled lib directory
- `QT_QPA_PLATFORM=offscreen` — Qt platform plugin for headless rendering

---

## Docs & Memory References

| File | Description |
|------|-------------|
| [docs/rewrite-plan.md](docs/rewrite-plan.md) | Full rewrite plan with Phases 0-6, architecture, API changes |
| [docs/bug-gap-report.md](docs/bug-gap-report.md) | 18 bugs + 10 feature gaps found in v1 analysis |
| [docs/session-analysis.md](docs/session-analysis.md) | Full session notes from initial code analysis |
| `/memories/repo/kobo-skeets-notes.md` | FBInk submodules, libatproto API details, build notes |
| `/memories/repo/skeets-codebase-overview.md` | Full codebase architecture (138 lines) |
| `/memories/repo/kobo-tls-fixes.md` | OpenSSL, VFAT, CA cert, DNS fixes documentation |
