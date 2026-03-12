# sKeets — Session State & Plan

> Written for IDE restart continuity. Last updated: 2026-03-12.
> Memory files: `/memories/repo/kobo-skeets-notes.md`, `/memories/repo/skeets-codebase-overview.md`, `/memories/repo/kobo-tls-fixes.md`

---

## Current State: Authentication COMPLETE, Feed Bootstrap WORKING

The rewrite now authenticates, restores saved sessions, loads the home
timeline, refreshes against AppView, and paginates on-device. Touch input on
the Clara Colour is working after enabling X-axis mirroring for the `snow`
protocol path. The rewrite is no longer just an auth bootstrap shell: it now
renders a usable text-first feed view with author, timestamp, body text, image
placeholders, and stats.

The current milestone is **media and preview layout validation after the
wrapped-text clipping fix**. Phase 3 interaction support is working in the
rewrite, and Phase 3.5 settings now cover both image-gated rendering and
default-off moderation preferences in feed and thread views. Saved-session
restore now uses the ATProto refresh-token path, so expired access JWTs are
rotated during bootstrap and during rewrite runtime requests instead of forcing
a relogin.

### Latest verified build

- **build_timestamp**: `2026-03-12` (latest successful package at the confirmed wrapped-text clipping-fix checkpoint)
- **Package**: `build-kobo/KoboRoot.tgz`
- **Build command**: `./docker-build.sh`

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
| **Phase 3** | Like and repost support | Working on-device |
| **Phase 3.5** | Settings view: image + moderation toggles | Working on-device with feed/thread image rendering and label gating |
| **Phase 4** | Input improvements (swipe, long-press) | Deferred again — swipe experiment removed after poor device behavior |
| **Phase 5** | Framebuffer & reliability improvements | Not started in the active build — later refresh experiments were rolled back |
| **Phase 5.5** | Bug fixes (JWT refresh, thread recursion, etc.) | Not started |
| **Phase 6** | Cleanup (README, docs, stale comments) | Not started |

### What is now verified on-device

1. `login.txt` is read from `/mnt/onboard/.adds/sKeets/login.txt`
2. `createSession()` succeeds against a self-hosted PDS (nihilist.cloud)
3. Session tokens saved to `config.ini` (handle, access_jwt, refresh_jwt, did, pds_url, appview_url)
4. `login.txt` securely deleted after reading (whether login succeeds or fails)
5. On relaunch, `resumeSession()` reads from `config.ini` and restores the session on-device
6. Timeline fetches use `appview_url` (`https://api.bsky.app`) instead of the PDS, which fixed stale refresh results
7. Feed refresh now pulls new data correctly on-device
8. Feed pagination works on-device
9. Clara Colour touch hit-testing works after enabling `SKEETS_REWRITE_TOUCH_MIRROR_X=1`
10. Feed like/repost actions work on-device
11. Thread like action works on-device
12. Stats labels now render as readable word labels (`<Like>`, `Reply`, `<Repost>`) instead of single-letter abbreviations
13. Active feed/thread actions use clearer flipped-caret states (`>Like<`, `>Repost<`) without changing hit-target widths
14. Feed and thread stat updates redraw only the affected stat row with partial e-ink refreshes
15. Feed and thread headers open a settings screen with persisted `profile_images_enabled` / `embed_images_enabled` toggles plus default-off moderation toggles for `allow_nudity_content`, `allow_porn_content`, and `allow_suggestive_content`
16. Feed and thread avatars render on-device through the shared async image cache when profile images are enabled
17. Feed image embeds render on-device when embed images are enabled
18. Thread view now renders image embeds when opening a post with media
19. Bluesky CDN image URLs are rewritten to `@jpeg`, avoiding WebP decode failures in the current embedded decoder path
20. Expired saved access tokens are now refreshed from the saved refresh token during bootstrap, feed/thread loads, and rewrite actions, and the rotated tokens are persisted back to `config.ini`
21. Feed and thread image embeds now preserve per-image alt text so the rewrite can show text fallbacks when embed images are disabled
22. Unsupported media embeds such as video or GIF-style posts now render a left-aligned preview thumbnail, when available, with alt text beside it instead of disappearing entirely
23. Non-full feed and thread repaints now refresh only the content region rather than the full screen, reducing unnecessary e-ink flashing during async image updates
24. Posts carrying matching moderation labels are now hidden by default in feed and thread until the corresponding Nudity, Porn, or Suggestive setting is enabled
25. Wrapped text in the active bitmap fallback path no longer clips or overlaps on-device; feed body text, alt-text blocks, and preview text now pre-measure against the same line breaks they render with

### What is still worth validating further

1. Settings toggles persist cleanly across multiple relaunches and sign-out/sign-in cycles
2. Async-loaded avatars and embeds repaint cleanly without objectionable artifacts in the current pre-Phase-5 refresh path
3. Thread layouts remain stable when larger images load after initial render
4. The cached JPEG coercion path remains robust across a wider mix of Bluesky CDN image URLs
5. Newly landed thread fixes still need on-device validation: thread-side repost/unrepost taps are now wired, and long threads now use a right-side scrollbar with pre-measured clipping instead of bottom paging controls or drawing past the bottom edge
6. Newly landed media fallback behavior still needs on-device validation: image alt-text blocks should appear when embed images are disabled, and unsupported media previews should keep reasonable thumbnail/text sizing now that the clipping bug is fixed
7. Newly landed moderation settings still need on-device validation: label-matched posts should show the hidden-content message by default and reappear immediately when the corresponding setting is enabled
8. Re-check the earlier screenshot-driven preview-card cases now that wrapped text is stable: unsupported-media thumb spacing, external preview sizing, and any remaining text/image overlap

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

### `src/kobo/main.cpp`
The main entry point for the current Kobo app. Changes this session:
- **dlopen OpenSSL preload**: Force-loads bundled OpenSSL 3.x with `RTLD_NOW | RTLD_GLOBAL` before Qt init. The earlier verbose probe spam was removed; only the preload result remains logged.
- **CA cert loading** (lines ~420-445): After QCoreApplication init, loads bundled CA certs from `<rewrite_dir>/ssl/certs/ca-certificates.crt` via `QSslCertificate::fromDevice()` and sets as default SSL config.
- **Localized stat refresh helpers**: Feed and thread action handlers redraw only the relevant stats row and trigger a partial or grayscale-partial framebuffer refresh for that rectangle.
- **Clearer active-state labels**: Interactive labels keep stable widths while showing active state via flipped carets (`>Like<`, `>Repost<`).
- **Settings view**: Adds a rewrite-native settings screen reachable from feed and thread headers, with persistent `profile_images_enabled`, `embed_images_enabled`, `allow_nudity_content`, `allow_porn_content`, and `allow_suggestive_content` toggles plus a sign-out action that clears the saved session.
- **Avatar/embed rendering**: Reuses the shared async image cache to draw feed/thread avatars and image embeds when the corresponding settings are enabled.
- **Alt-text fallbacks**: Image embeds now retain per-image alt text and render text-only fallback blocks when embed images are disabled.
- **Unsupported media previews**: Video/GIF-style embeds now surface a smaller preview thumbnail, when available, with alt text laid out to its right.
- **Moderation gating**: Posts now retain non-negated ATProto label values so feed and thread rendering can hide nudity, porn, or suggestive content by default until the matching setting is enabled.
- **Wrapped-text clipping fix**: The active bitmap fallback text path now measures wrapped blocks from the same word-wrap logic it draws, which fixed the remaining on-device clipping/overlap seen around feed bodies and preview text.
- **CDN compatibility fix**: Rewrites Bluesky CDN image URLs to request `@jpeg` so the current decoder path can render avatars and embeds on-device.
- **Thread media rendering**: The thread view now renders image embeds as well as feed cards, rather than dropping media after navigation.
- **Async redraw handling**: Pumps Qt events in the main loop and triggers a repaint when `image_cache_redraw_needed()` signals that a download completed.
- **Bounded partial redraws**: Non-full feed and thread repaints now refresh only the content region instead of the whole screen.
- **Thread scrollbar and overflow fix**: Thread rendering now pre-measures each visible post, stops before drawing beyond the bottom edge, and uses a right-side scrollbar as the navigation surface for longer reply chains.
- **Thread repost handling**: Thread stat hit targets now include repost/unrepost actions, matching the feed interaction surface.
- **Swipe rollback**: The earlier swipe experiment was removed after poor on-device behavior; navigation remains tap-driven for now.

### `src/util/image_cache.cpp` / `src/util/image.cpp`
- The shared image cache now logs concise fetch/decode state, retries transient failures, reopens raw disk-cache files correctly, and normalizes Bluesky CDN image URLs to `@jpeg` before hashing, fetch, and cache storage.

### `src/ui/font.cpp`
- Wrapped-text measurement and drawing now share the same line-breaking logic in the active bitmap fallback path, which fixed the last on-device clipping/overlap regression after preview cards and alt-text blocks were introduced.

### `CMakeLists.txt`
- The rewrite target now compiles and links the shared `src/ui/fb.cpp`, `src/util/image.cpp`, and `src/util/image_cache.cpp` sources, and links the `stb_image` interface dependency so the rewrite can reuse the legacy image pipeline.

### `src/kobo/bootstrap.cpp`
The auth bootstrap logic. Changes this session:
- **saved-session retry path**: Retries transient DNS failures before declaring resume failure and preserves the saved-session error state instead of falling back to "waiting for login.txt"
- **refresh-token restore path**: Uses the SDK's resume-and-refresh flow so `ExpiredToken` on `getSession` rotates the saved JWTs instead of failing authentication
- **Include added**: `<fstream>`

### `src/kobo/feed.cpp`
The feed loader. Changes this session:
- Fetches the timeline through `session.appview_url` instead of the PDS
- Restores saved sessions with retry handling for transient DNS failures and refresh-token fallback for expired access JWTs
- Filters plain reply posts out of the home feed (unless the item is a repost)
- Uses a fresh AppView client directly to avoid heap corruption in `AtprotoClient::changeHost()`

### `src/kobo/actions.cpp`
The action helper. Changes this session:
- Performs like/unlike and repost/unrepost mutations against the PDS host
- Reuses the saved-session retry pattern before mutating records and persists refreshed JWTs when the restore path rotates them
- Keeps AppView reads and PDS writes separate to avoid host-switch instability

### `run.sh`
The main launcher. Changes this session:
- Auto-enables `SKEETS_REWRITE_TOUCH_MIRROR_X=1` for Kobo Clara Colour (`spaColour`) using the `snow` touch protocol
- Logs `Touch mirror X/Y` to make touch transform verification visible on-device

### `cmake/package-runtime-libs.cmake`
Library packaging for KoboRoot archive. Changes this session:
- Replaced all `file(CREATE_LINK ... SYMBOLIC)` with `file(COPY_FILE ... ONLY_IF_DIFFERENT)` for libssl.so and libcrypto.so in both `DEST_LIB_DIR` and `DEST_APP_DIR/lib`

### `CMakeLists.txt`
- The main `sKeets` target now links `dl` (for dlopen/dlsym)

### `Dockerfile.cross`
- Added `RUN cat /etc/ssl/certs/*.pem > /etc/ssl/certs/ca-certificates.crt` to rebuild CA bundle (this turned out to be unnecessary — the bundle was already complete, but it's harmless)

---

## Current Architecture

The active Kobo app now lives in dedicated `src/kobo/` and `src/platform/` directories:

```
src/kobo/
├── main.cpp                  Entry point + OpenSSL preload + CA loading + UI event loop
├── bootstrap.h/cpp           Auth flow: login.txt → createSession / config.ini → resumeSession
├── tool_main.cpp             Diagnostic tool entry point
├── diag_main.cpp             Hardware diagnostics entry point
├── actions.h/cpp             Like/repost mutations and session refresh helpers
├── feed.h/cpp                Timeline loading and pagination helpers
└── thread.h/cpp              Thread loading helpers

src/platform/
├── device.h/cpp              Device detection (model, codename, platform, touch protocol)
├── framebuffer.h/cpp         FBInk-based framebuffer abstraction
├── input.h/cpp               Touch input via evdev (supports snow protocol for Clara Colour)
├── network.h/cpp             Network state probing (carrier, DNS, addresses)
└── power.h/cpp               Battery/charging state probing
```

### Key types in the rewrite

- `rewrite_framebuffer_t` — FBInk framebuffer context with screen dimensions and font metrics
- `rewrite_input_t` — evdev input context with coordinate normalization
- `rewrite_bootstrap_result_t` — Auth bootstrap outcome (state enum + session + messages)
- `rewrite_device_info_t` — Device hardware info
- `rewrite_power_info_t` / `rewrite_network_info_t` — Runtime hardware state
- `rewrite_app_t` — Main app state (all of the above + UI state)

### Build targets

- `sKeets` — main Kobo app binary emitted by the rewrite target
- `sKeets-diag` — hardware diagnostic binary
- `sKeets-tool` — rewrite support tool binary
- `kobo-package` — packages the active app + libs + assets into KoboRoot.tgz

### External dependencies

- **libatproto** (mfnboer/atproto, pinned SHA 2a1019b) — C++ ATProto SDK
- **Qt 6.5.2** — Core, Network, Qml (cross-compiled for ARM)
- **FBInk** — E-ink framebuffer library (built as ExternalProject)
- **OpenSSL 3.0.13** — bundled in package for TLS
- **stb_image** — Image decoding (not yet used in rewrite)

---

## Immediate Next Steps

### 1. Current checkpoint: clipping fixed, media layout next
Status: wrapped-text clipping fix verified on-device.

Implemented result:
- Feed and thread headers expose a `Settings` action
- The rewrite has a dedicated settings screen with Back and Sign Out actions
- `profile_images_enabled` defaults to on, while `embed_images_enabled`, `allow_nudity_content`, `allow_porn_content`, and `allow_suggestive_content` default to off, and all five values persist in `config.ini`
- Feed and thread avatars render through the shared async image cache when profile images are enabled
- The embed-images toggle controls image embed rendering in both feed and thread views
- The rewrite repaints automatically when async image downloads complete
- When embed images are disabled, the rewrite now shows per-image alt text instead of dropping image attachments entirely
- Unsupported media embeds now show a smaller preview thumbnail, when available, with alt text beside it
- Posts with matching moderation labels now replace their body/embed area with a hidden-content message that points the user back to Settings
- Wrapped text in the active bitmap fallback path no longer clips or overlaps on-device after the draw/measure mismatch was removed
- Bluesky CDN image URLs are rewritten to `@jpeg` to avoid WebP decode failures in the current decoder path

Remaining polish:
- Re-check unsupported-media and external preview-card sizing/spacing on-device now that text flow is stable
- Confirm async-loaded avatars and embeds repaint cleanly in the current baseline build before attempting more refresh work
- Confirm toggles persist across relaunches and sign-out/sign-in cycles
- Confirm moderation labels seen in the wild map cleanly onto the Nudity, Porn, and Suggestive buckets without obvious false positives
- Consider a cleaner image decode path if WebP support is needed later without CDN coercion

### 2. Phase 3 interaction polish

- Feed like/unlike and repost/unrepost use optimistic updates with rollback on failure
- Thread view has tappable like targets
- Thread view now has code-level repost/unrepost tap handling as well; this still needs device validation
- Stats labels are word-based and more readable: `<Like>`, `Reply`, `<Repost>`
- Post stat updates use localized partial refreshes instead of full-screen redraws
- Active-state styling is clearer via flipped-caret labels while preserving stable hit-target widths

### 3. Next candidate milestone
- Validate the new right-side thread scrollbar and thread repost behavior on-device
- Validate image alt-text fallback blocks and unsupported media preview rows on-device
- Re-audit preview-card and media spacing against the original screenshots now that clipping is no longer obscuring the layout bugs
- Defer Phase 5 refresh work until the current clipping-fix/media-layout baseline is fully characterized on-device
- Defer a future settings refinement for the thread scrollbar: make it slightly wider and add pagination buttons at either end, while keeping left-side placement as a later settings option
- Consider replacing the current feed-side reply filter with a richer policy once thread view is in place

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
# Build KoboRoot.tgz for the main package
./docker-build.sh

# The output is at build-kobo/KoboRoot.tgz
# Copy to Kobo's .kobo/ directory for auto-extraction on disconnect:
cp build-kobo/KoboRoot.tgz /media/$USER/KOBOeReader/.kobo/KoboRoot.tgz

# Or manually extract to test directory:
# The package extracts to /mnt/onboard/.adds/sKeets/
```

### Environment variables on device

Set in the packaged launcher scripts:
- `SKEETS_DATA_DIR` — data directory path
- `SKEETS_REWRITE_DIR` — app directory (default: `/mnt/onboard/.adds/sKeets`)
- `SKEETS_REWRITE_TOUCH_PROTOCOL=snow` — for Clara Colour touch input
- `LD_LIBRARY_PATH` — includes bundled lib directory
- `QT_QPA_PLATFORM=offscreen` — Qt platform plugin for headless rendering

---

## Docs & Memory References

| File | Description |
|------|-------------|
| [docs/atproto-patches.md](docs/atproto-patches.md) | Local libatproto patches, wrapper adaptations, and runtime workarounds |
| [docs/rewrite-plan.md](docs/rewrite-plan.md) | Full rewrite plan with Phases 0-6, architecture, API changes |
| [docs/bug-gap-report.md](docs/bug-gap-report.md) | 18 bugs + 10 feature gaps found in v1 analysis |
| [docs/session-analysis.md](docs/session-analysis.md) | Full session notes from initial code analysis |
| `/memories/repo/kobo-skeets-notes.md` | FBInk submodules, libatproto API details, build notes |
| `/memories/repo/skeets-codebase-overview.md` | Full codebase architecture (138 lines) |
| `/memories/repo/kobo-tls-fixes.md` | OpenSSL, VFAT, CA cert, DNS fixes documentation |
