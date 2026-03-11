# sKeets Rewrite — Full Session Analysis

> Comprehensive notes from the analysis session covering NickelHook, NickelMenu,
> KOReader-base, Kobo-Reader, sKeets internals, ATProto SDK, and identified bugs.

---

## 1. Corrected Requirements (Final)

- **Login**: ATProto authentication via `login.txt` file (handle + app password), no interactive login UI
- **Feed browsing**: Scroll feeds showing author, post time, content, rendered embeds
- **Profile images**: Shown **by default**
- **Embed images**: Toggled on/off in settings menu (separate from profile images)
- **Post interactions in feed**: **Like** and **Repost**
- **Thread view**: Click a post to browse comments; interaction in comments is **Like only**
- **Removed features**: No posting, no replying, no compose view

---

## 2. NickelHook Analysis

**Purpose**: PLT/GOT hooking library that injects code into Kobo's Nickel reader via a fake Qt image plugin.

### Mechanism
- `nh.c` registers a Qt image plugin (via `Q_IMPORT_PLUGIN` / `QImageIOPlugin`)
- Nickel loads it from `/usr/local/Kobo/imageformats/` during startup
- The plugin's factory function (`nh_init`) runs in Nickel's process space
- `nh_dlsym()` resolves mangled C++ symbols from libnickel.so.1.0.0 for hooking

### Key API
```c
typedef struct nh_info {
    int  (*init)(void *);     // plugin initializer
    int  (*uninstall)(void *); // uninstall callback
    int    uninstall_flag;     // set by failsafe
} nh_info;

typedef struct nh_dlsym_t {
    const char *sym;  // mangled symbol name
    void **out;       // where to store resolved address
} nh_dlsym_t;
```

### Failsafe
- Checks `/mnt/onboard/.adds/nm/failsafe` on each boot
- If present, runs uninstall callback and stops loading hooks
- Prevents bricking if a hook causes boot loops

### Toolchain
- `NickelTC`: `arm-nickel-linux-gnueabihf` cross-compiler
- Targets Qt Embedded 4.6 (Nickel's version), C with gnu11

### Relevance to sKeets
- sKeets cannot use NickelHook directly — it's Qt6/C++23 vs Nickel's Qt4.6
- However, NickelMenu (which builds on NickelHook) can *launch* sKeets as a standalone process via `cmd_spawn`

---

## 3. NickelMenu Analysis

**Purpose**: Injects custom menu items into Nickel's UI, allowing users to launch external programs.

### Hooking target
- Hooks `AbstractNickelMenuController::createMenuTextItem()` in libnickel
- Symbol: `_ZN28AbstractNickelMenuController18createMenuTextItemEP7QWidgetRK7QStringbb`
- Injects items after Nickel creates its own menu entries

### Config format
Located at `/mnt/onboard/.adds/nm/config`:
```
menu_item:main:sKeets:cmd_spawn:quiet:/mnt/onboard/.adds/sKeets/run.sh
```

### Launch mechanism
- `cmd_spawn` uses `QProcess::startDetached("/bin/sh", {"-c", cmd})`
- `quiet` prefix suppresses "successfully started" dialog
- The spawned process runs independently of Nickel

### Source structure
- `nickelmenu.cc`: Main hook registration, menu injection
- `action.c` / `action_cc.cc`: Command handlers (cmd_spawn, cmd_output, cmd_toast, etc.)
- `config.c`: Config file parser for menu item definitions
- `generator.c`: Creates QWidgets for injected menu items
- `kfmon.c` / `kfmon.h`: Integration with KFMon (book-file-based app launcher)

### Relevance to sKeets
- Primary launch mechanism: NickelMenu `cmd_spawn` calls `run.sh`
- `run.sh` then kills Nickel, takes over framebuffer, feeds watchdog
- Alternative: KFMon trigger via a `.kobo/` dummy book file

---

## 4. KOReader-base Analysis — Kobo Platform Layers

### Framebuffer abstraction
KOReader-base has a multi-layer FB architecture:

1. **Generic Linux FB** (`ffi/framebuffer_linux.lua`): mmap `/dev/fb0`, FBIOGET_VSCREENINFO for resolution/bpp
2. **mxcfb backend** (`ffi/framebuffer_mxcfb.lua`): NXP i.MX EPDC — `MXCFB_SEND_UPDATE` ioctl with waveform modes (GC16, DU, A2, REAGL)
3. **sunxi backend**: Allwinner — `DISP_EINK_UPDATE2` ioctl, EINK_INIT_MODE for cold start
4. **MTK backend**: MediaTek — `HWTCON_SEND_UPDATE` ioctl, `HWTCON_WAIT_FOR_VSYNC`

### Input handling
- Opens `/dev/input/event*` with `EVIOCGRAB` (exclusive access)
- Batch buffer reads: `ffi.C.read(fd, ev_buf, batch_size)` — reads many events at once
- Uses `timerfd_create()` for gesture timing (tap vs drag vs long-press)
- `uevent.lua`: Forks a child process that monitors USB plug/unplug and battery events, converts them to fake input events via a pipe so the main loop can handle them uniformly

### Device quirk handling
KOReader maintains per-device tables:
- Touch axis transforms: swap_xy, mirror_x, mirror_y per model
- Screen rotation compensation
- EPDC quirks: some devices need specific waveform sequences
- Color panel detection and handling

### E-ink refresh waveform modes
| Mode | Speed | Quality | Use case |
|------|-------|---------|----------|
| GC16 | Slow | Best, 16 grays | Full page, images |
| DU | Fast | Monochrome only | Text scroll |
| A2 | Fastest | Binary B&W | Animation, cursor |
| REAGL | Medium | Good, reduced ghost | Partial updates (some HW) |
| GC16_FAST | Medium | Acceptable | Quick mixed updates |

### Key differences from sKeets
| Aspect | KOReader-base | sKeets |
|--------|---------------|--------|
| Language | LuaJIT + FFI | C++23 + Qt6 |
| FB access | Direct mmap | Via FBInk library |
| Input | Batch reads + timerfd | Single-event poll() |
| Device quirks | Per-model tables | Heuristic only |
| Touch transform | Explicit tables | Axis-span guessing |
| Gestures | Full (tap, drag, swipe, pinch, long-press) | Tap only |

---

## 5. Kobo-Reader Analysis

**Purpose**: Open-source build tree with cross-compilation toolchain and plugin examples for Kobo.

### Build system
- `build/build-all.sh`: Master build script
- `build/build-config.sh`: Toolchain paths, sysroot config
- `build/build-common.sh`: Shared build functions

### Plugin examples
- `examples/PluginInterface.h`: QWidget-based plugin interface for Nickel
- `examples/blackjack/` and `examples/poker/`: Game plugins using the plugin interface
- These demonstrate how to create QWidget UI inside Nickel's process

### Toolchain
- `toolchain/`: Cross-compilation setup for ARM Kobo targets
- Targets the same `arm-linux-gnueabihf` platform as sKeets

### `fickel/`
- `fickel.pro`: Qt project file — a standalone app using Qt widgets
- `main.cpp` / `main.h`: Example standalone Qt application for Kobo

### Relevance
- Confirms Qt4.6 plugin model for Nickel integration (not usable by sKeets)
- Build scripts could inform cross-compilation improvements
- The toolchain setup is a reference for the Docker build environment

---

## 6. sKeets Source Audit — Complete File Inventory

### Entry point & app state

**src/main.cpp** (~50 lines):
- Signal handlers for SIGTERM/SIGINT → `app_state.running = false`
- Sets locale to `C.UTF-8`
- Creates `QCoreApplication` (no GUI)
- Calls `app_init(&state)` → `app_run(&state)` → `app_shutdown(&state)`

**src/app.h** (~80 lines):
- `app_state_t` struct: `current_view`, `session`, `feed`, `selected_post`, `compose_reply_uri/cid`, `compose_root_uri/cid`, `images_enabled`, `running`, `fb`, `input`, `feed_scroll`, `thread_scroll`, `status_msg`, `atproto_client`
- Functions: `app_init`, `app_run`, `app_shutdown`, `app_switch_view`, `app_ensure_auth`

**src/app.cpp** (~200 lines):
- `app_init()`: device detection, `fb_open()`, `font_init(ot=false)`, `input_open()`, config load, session resume or auto-login from config values
- `app_run()`: Main loop — `QCoreApplication::processEvents()` → `image_cache_redraw_needed()` check → `input_poll()` → view dispatch (`_draw` + `_handle`)
- `app_switch_view()`: Clears view-specific state, loads data for new view
- `app_ensure_auth()`: Checks session validity, redirects to login if needed

### UI layer

**src/ui/fb.h** (~60 lines):
- `FB_WIDTH = 1072`, `FB_HEIGHT = 1448` ← **HARDCODED, BUG SOURCE**
- `fb_t` struct: `fbink_fd`, `width`, `height` (from FBInk runtime)
- Functions: `fb_open`, `fb_close`, `fb_clear`, `fb_fill_rect`, `fb_draw_rect`, `fb_hline`, `fb_vline`, `fb_blit_rgba`, `fb_refresh_full`, `fb_refresh_partial`, `fb_refresh_fast`, `fb_load_fonts`

**src/ui/fb.cpp** (~240 lines):
- `fb_open()`: Opens FBInk, gets `FBInkState`, stores `screen_width`/`screen_height` into `fb_t`
- All drawing uses `fbink_fill_rect_gray()` with `rgb565_to_gray()` conversion
- `fb_blit_rgba()`: Blits RGBA buffer with gray conversion
- Refresh modes: GC16 full+flash, DU partial, A2 fast binary

**src/ui/font.h** (~30 lines):
- `FONT_CHAR_W = 8`, `FONT_CHAR_H = 16` ← **HARDCODED, BUG SOURCE**
- Functions: `font_init`, `font_set_ot_enabled`, `font_draw_char`, `font_draw_string`, `font_draw_string_wrapped`, `font_measure_string`, `font_measure_string_wrapped`

**src/ui/font.cpp** (~240 lines):
- `s_screen_w = 1448`, `s_screen_h = 1072` ← **NOTE: WIDTH/HEIGHT SWAPPED from fb.h!**
- `font_draw_string_fallback()`: Converts pixel coords to FBInk grid: `cfg.row = y / FONT_CHAR_H`, `cfg.col = x / FONT_CHAR_W`, with hoffset/voffset for sub-cell positioning
- On high-DPI Kobos, FBInk scales the VGA font by `fontsize_mult`, making actual cell size `8*mult × 16*mult` — but the hardcoded constants don't account for this
- `font_draw_string_ot()`: Uses `fbink_print_ot` with `FBInkOTConfig` margins (works correctly since it uses pixel coords directly)

**src/ui/input.h** (~50 lines):
- `input_state_t`: `fds[]`, `nfds`, `abs_x/y_min/max`, `swap_axes`, `pending_touch`, `touch_down_x/y/time`
- `touch_event_t`: `DOWN`, `UP`, `MOVE`
- `kobo_key_t`: `POWER`, `FORWARD`, `BACK`, `HOME`
- `input_event_t`: union of touch/key events

**src/ui/input.cpp** (~260 lines):
- `input_open()`: Opens all `/dev/input/event*`, probes axes via `EVIOCGABS`, `EVIOCGRAB`
- `should_swap_axes()`: Compares axis spans to `FB_WIDTH`/`FB_HEIGHT` ← **USES HARDCODED DIMS**
- `normalize_axis()`: Maps raw touch value to `[0, FB_WIDTH)` or `[0, FB_HEIGHT)` ← **HARDCODED**
- `input_poll()`: `poll()` all fds, reads one `input_event`, processes ABS_MT_POSITION_X/Y or BTN_TOUCH/KEY events
- `input_is_tap()`: Manhattan distance ≤ `TAP_MAX_DIST` (30px) and time ≤ `TAP_MAX_MS` (300ms)

**src/ui/views.h** (~40 lines):
- `app_view_t`: `VIEW_LOGIN`, `VIEW_FEED`, `VIEW_THREAD`, `VIEW_COMPOSE`, `VIEW_SETTINGS`
- Layout constants: `UI_MARGIN=16`, `UI_PAD=8`, `UI_BAR_H=52`, `UI_MIN_TAP=44`, `FEED_PAGE_SIZE=50`
- Helper: `clamp_scroll()`, `hit_test_rect()`

**src/ui/login_view.h/cpp** (~225 lines):
- Three fields: handle, password, PDS URL (default `bsky.social`)
- Field focus via tap, sign-in button
- `login_view_prefill()`: Pre-fills from config values
- Status: **TO BE REPLACED** by auth_view

**src/ui/feed_view.h/cpp** (~250 lines):
- Post height caching: `s_post_heights[]`, `s_post_y[]`, `s_virtual_height`
- `measure_post_height()`: Calculates height for author line + wrapped text + embeds
- `draw_post()`: Renders avatar (if images enabled), author + time, wrapped text, embeds (Image, Quote, RecordWithMedia, External), stats line (♡ ↩ ⟳)
- Top bar: Refresh, Home, +Post, Settings
- Touch: scroll on drag, tap topbar buttons, tap post body → thread view
- **No like/repost interaction currently** — stats line is display-only

**src/ui/thread_view.h/cpp** (~240 lines):
- Flattens thread tree into `draw_node_t` array (max 128 nodes)
- Indent levels via `THREAD_INDENT = 32` pixels
- Top bar: Back, Thread title, Reply button
- Tap post → opens compose for reply
- **No like interaction currently**

**src/ui/compose_view.h/cpp** (~150 lines):
- Text area with cursor, 300 char limit, Post/Cancel buttons
- Reply context displayed at top
- Status: **TO BE DELETED**

**src/ui/settings_view.h/cpp** (~160 lines):
- Toggle: `images_enabled` (currently controls all images)
- Sign-out button (clears session from config)
- Version string display
- Status: **NEEDS UPDATE** — split into profile_images + embed_images toggles

### ATProto layer

**src/atproto/atproto.h** (~80 lines):
```cpp
Bsky::Author { did, handle, display_name, avatar_url }
Bsky::Post {
    uri, cid, author, text, indexed_at,
    like_count, reply_count, repost_count,
    embed_type (NONE, IMAGE, QUOTE, EXTERNAL, RECORD_WITH_MEDIA),
    quoted_post, image_urls[4],
    ext_uri, ext_title, ext_description, ext_thumb_url,
    reply_parent_uri, reply_root_uri,
    replies (vector<Post>)
}
Bsky::Feed { items (vector<Post>), cursor }
Bsky::Session { access_jwt, refresh_jwt, did, handle, display_name, avatar_url, pds_url }
```

**src/atproto/atproto_client.h/cpp** (~280 lines):
- Wraps mfnboer/atproto SDK
- `createSession(handle, password, pds_url)` → `Bsky::Session`
- `resumeSession(access_jwt, refresh_jwt, did, pds_url)` → bool
- `getTimeline(cursor)` → `Bsky::Feed`
- `getPostThread(uri)` → `Bsky::Post` (with replies tree)
- `createPost(text, reply_parent, reply_root)` → bool
- `changeHost(pds_url)`, `hasSession()` → bool
- All use `QEventLoop` for synchronous execution
- `convertPostView()`: Extracts author, text, embeds, counts — **does NOT extract ViewerState (mLike/mRepost)**

### Utility modules

**src/util/paths.h/cpp**:
- `SKEETS_DATA_DIR` env → defaults `/mnt/onboard/.adds/sKeets`
- `skeets_data_dir()`, `skeets_config_path()`, `skeets_cache_dir()`, `skeets_ensure_data_dirs()`

**src/util/config.h/cpp**:
- INI key=value parser, max 64 entries
- `config_load(path)`, `config_save(path)`, `config_get/set_str/int/bool(key)`

**src/util/device.h/cpp**:
- `device_kobo_version_string()`: Reads `/mnt/onboard/.kobo/version`
- `device_kobo_model_string()`: Extracts model from version string

**src/util/image.h** + **image_cache.h**:
- `image_t`: RGBA pixel buffer with width/height
- `load_url()`: Qt NAM async download
- `load_file()`: stb_image decode from disk
- `scale_to_fit()`: Bilinear downscale to max dimensions
- `decode_memory()`: stb decode from memory buffer
- Disk cache: SKIC header + raw RGBA in `cache/` dir
- `image_cache_lookup()`: Check memory → disk → network pipeline
- `image_cache_redraw_needed()`: Flag polled by main loop

**src/util/str.h/cpp**:
- `str_safe_copy()`, `str_trim()`, `str_empty()`, `str_format_time()`

### Build & deployment

**CMakeLists.txt** (~190 lines):
- `cmake_minimum_required(VERSION 3.16)`
- `FetchContent` for `atproto` SDK (pinned SHA 2a1019b) and `stb`
- `ExternalProject_Add` for FBInk (KOBO=1 IMAGE=1 OPENTYPE=1)
- Resolves toolchain sysroot libs: ld-linux, libc, libm, libstdc++, libgcc_s, libz, libssl, libcrypto
- Custom target `kobo-package`: Creates `KoboRoot.tgz` with binary + libs + fonts + run.sh

**run.sh** (~70 lines):
- Kills Nickel + all companion processes
- Feeds `/dev/watchdog` every 10s in background
- Sets `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`, `SSL_CERT_FILE`, `SKEETS_FONT_DIR`
- Runs sKeets via custom `ld-linux-armhf.so.3` loader
- Reboots device on exit via trap

**Dockerfile.cross**: Docker image for ARM cross-compilation
**docker-build.sh**: Convenience wrapper for Docker build
**Makefile**: Top-level make targets

---

## 7. ATProto SDK — Like and Repost API

The sKeets project uses `mfnboer/atproto` SDK (pinned at SHA `2a1019b`).

### PostMaster class (relevant methods)

```cpp
// Like a post
void PostMaster::like(
    const QString& uri, const QString& cid,
    const QString& viaUri, const QString& viaCid,
    const SuccessCb& successCb, const ErrorCb& errorCb);

// Repost a post
void PostMaster::repost(
    const QString& uri, const QString& cid,
    const QString& viaUri, const QString& viaCid,
    const SuccessCb& successCb, const ErrorCb& errorCb);

// Undo like or repost (pass the like/repost record URI)
void PostMaster::undo(
    const QString& uri,
    const SuccessCb& successCb, const ErrorCb& errorCb);
```

### ViewerState (app_bsky_feed.h, line 14)

```cpp
struct ViewerState {
    std::optional<QString> mRepost;  // repost record URI if user reposted
    std::optional<QString> mLike;    // like record URI if user liked
    bool mBookmarked = false;
    bool mThreadMuted = false;
    bool mReplyDisabled = false;
    bool mEmbeddingDisabled = false;
    bool mPinned = false;
};
```

### What sKeets currently extracts
- `convertPostView()` extracts: author, text, embeds, like/reply/repost counts
- **Does NOT extract**: `mViewer->mLike`, `mViewer->mRepost` (ViewerState)
- This means sKeets cannot currently know if the user has already liked/reposted a post

### What needs to be added
1. `viewer_like` field (string, like record URI) on `Bsky::Post`
2. `viewer_repost` field (string, repost record URI) on `Bsky::Post`
3. Extract `mViewer->mLike` and `mViewer->mRepost` in `convertPostView()`
4. `likePost(uri, cid)` / `unlikePost(like_uri)` methods on `AtprotoClient`
5. `repostPost(uri, cid)` / `unrepostPost(repost_uri)` methods on `AtprotoClient`

---

## 8. Three Coordinate Misalignment Bugs

### Bug 1 — Hardcoded screen dimensions in fb.h

**File**: `src/ui/fb.h`
**Problem**: `FB_WIDTH=1072`, `FB_HEIGHT=1448` are compile-time constants, but FBInk reports actual screen dimensions at runtime via `FBInkState.screen_width`/`screen_height`. On some Kobo models (especially NTX devices with landscape-native panels), the actual values may differ due to rotation or panel orientation.

**Evidence**: `fb_open()` in fb.cpp stores `state.screen_width`/`state.screen_height` into `fb_t.width`/`fb_t.height`, but views and input use `FB_WIDTH`/`FB_HEIGHT` constants instead.

**Fix**: Remove `FB_WIDTH`/`FB_HEIGHT` macros. All code must use `fb->width`/`fb->height` from the runtime-initialized struct.

### Bug 2 — Font cell size mismatch

**File**: `src/ui/font.cpp`
**Problem**: `FONT_CHAR_W=8`, `FONT_CHAR_H=16` assume the native VGA 8×16 font, but on high-DPI Kobos, FBInk applies `fontsize_mult` scaling. The actual rendered cell size is `8*mult × 16*mult`.

**Code path**: `font_draw_string_fallback()` computes FBInk grid position as:
```cpp
cfg.row = y / FONT_CHAR_H;  // divides by 16, but actual cell might be 32 or 48
cfg.col = x / FONT_CHAR_W;  // divides by 8, but actual cell might be 16 or 24
```

This means text renders at wrong positions — too far right and too far down.

**Fix**: Query `FBInkState.font_w` and `FBInkState.font_h` after `fb_open()`, store in `fb_t`, pass to font layer. Replace `FONT_CHAR_W`/`FONT_CHAR_H` with runtime values.

### Bug 3 — Touch coordinates normalized to wrong range

**File**: `src/ui/input.cpp`
**Problem**: `normalize_axis()` maps raw touch values to `[0, FB_WIDTH)` or `[0, FB_HEIGHT)` using the hardcoded constants. If actual screen dimensions differ, touch coordinates won't match drawn content.

Also: `should_swap_axes()` compares axis spans against `FB_WIDTH`/`FB_HEIGHT` to decide if X/Y should be swapped. Wrong reference values → potentially wrong swap decision.

**Fix**: Pass actual `fb->width`/`fb->height` to `input_open()`. Use runtime dimensions for normalization and swap heuristic.

### Additional issue in font.cpp

The static variables `s_screen_w=1448`, `s_screen_h=1072` in font.cpp have width and height **swapped** compared to `FB_WIDTH=1072`, `FB_HEIGHT=1448` in fb.h. This suggests confusion about portrait vs landscape orientation. These should also be replaced with runtime values from `fb_t`.

---

## 9. Feature Gaps Identified Before Fresh Analysis

### Missing from current sKeets
1. **Like interaction** — stats line is display-only, no tap handler
2. **Repost interaction** — not implemented at all
3. **ViewerState extraction** — can't show if user already liked/reposted
4. **Swipe gestures** — only tap is detected, no swipe-to-scroll
5. **Long-press** — not detected
6. **Profile image vs embed image distinction** — single `images_enabled` toggle
7. **File-based login** — currently uses interactive text fields
8. **Proper device-model touch tables** — relies on axis-span heuristic
9. **JWT token refresh** — access tokens expire ~2h, no proactive refresh
10. **Thread reply depth** — only one level of replies extracted, nested threads lost
11. **Repost attribution** — timeline reposts shown without "reposted by" label
12. **Feed pagination** — only loads 50 posts, no "load more"
13. **Suspend/resume** — no power management, device stays awake

### Missing from current sKeets (e-ink specific)
1. **fb_wait_for_complete()** — no synchronization after refresh
2. **Dirty region tracking** — always refreshes entire screen
3. **GC16 partial refresh** — only has full GC16 (flashing), DU, and A2
4. **Grayscale model** — uses RGB565 internally despite e-ink being grayscale
5. **Partial refresh for interactions** — toggling like/repost triggers full-screen flash
6. **Image redraw resets views** — `app_switch_view` reinitializes view state on image cache miss

### Bugs found in fresh analysis (not in original plan)
1. **Font line buffer overflow** — `font_draw_wrapped_fallback()` 512-byte buffer, no bounds check
2. **Unbounded disk cache** — no size limit or eviction on eMMC storage
3. **Naive in-memory eviction** — hash-map order, not LRU
4. **Config entry limit** — silently drops keys beyond 64 entries
5. **Hash collision risk** — djb2a hash used as sole image cache key

---

## 10. Implementation Notes for Rewrite

### Repost button in feed view
- Display: `⟳ N` with filled icon when reposted
- Hit zone: 44×44px (meets UI_MIN_TAP)
- Tap: optimistic toggle → async `PostMaster::repost()` or `PostMaster::undo(viewer_repost_uri)`
- Store `viewer_repost` URI on `Bsky::Post` to track state

### Repost NOT in thread view
- Per requirements, thread/comments interactions are like-only
- Thread view stats line shows repost count but is not tappable

### Profile images vs embed images
- Two separate settings: `profile_images_enabled` (default: true), `embed_images_enabled` (default: false)
- Profile images = author avatars in feed and thread
- Embed images = post image attachments and external link thumbnails
- Settings view needs two toggle rows instead of one

### Login file flow
- `login.txt` at `$SKEETS_DATA_DIR/login.txt`
- Format: key=value lines — handle (required), app password (required), pds_url (optional, default bsky.social), appview (optional, default `https://api.bsky.app`)
- Login attempt happens automatically in `app_init()` when file is found
- Auth view is a static instructions screen with Exit button only (no Retry — editing the file requires USB, so user must restart sKeets)
- On success: create session, save tokens + appview to config, delete login.txt
- On failure: delete login.txt, show error on auth_view, user must fix file and relaunch

---

## 11. Run Environment Details

### run.sh behavior
1. Kills: `nickel`, `hindenburg`, `sickel`, `fickel` and other Kobo daemons
2. Feeds `/dev/watchdog` every 10 seconds (prevents hardware reboot)
3. Sets `LD_LIBRARY_PATH` to sKeets lib dir + system lib dirs
4. Sets `QT_PLUGIN_PATH`, `SSL_CERT_FILE`, locale
5. Runs binary through custom `ld-linux-armhf.so.3` (packaged in KoboRoot.tgz)
6. On exit (trap): reboots device to restore Nickel

### KoboRoot.tgz package contents
- `usr/local/sKeets/sKeets` — main binary
- `usr/local/sKeets/lib/` — Qt6 + OpenSSL + system libs
- `usr/local/sKeets/fonts/` — OTF fonts for FBInk
- `usr/local/sKeets/run.sh` — launcher script
- Installed to device via `.kobo/KoboRoot.tgz` sideloading mechanism

### Device support
- Primary target: Kobo Clara Colour (NTX, i.MX, color e-ink panel)
- Also should work on: Clara HD, Libra 2, Elipsa, Sage
- `device.cpp` reads `/mnt/onboard/.kobo/version` for model identification

---

## 12. External Codebase References

### KOReader patterns worth adopting
1. **Per-device touch transform tables** instead of axis-span heuristic
2. **Batch event reads** instead of single-event poll
3. **timerfd for gesture timing** instead of manual timestamp comparison
4. **uevent monitoring** for USB/power events (currently ignored by sKeets)
5. **Dirty region tracking** for efficient partial refreshes

### FBInk API surface (key structs/functions)

```c
// State query
void fbink_get_state(const FBInkConfig*, FBInkState*);
// FBInkState fields:
//   screen_width, screen_height
//   font_w, font_h (actual cell size after scaling)
//   fontsize_mult
//   glyph_width, glyph_height
//   max_cols, max_rows
//   is_ntx_quirky_landscape
//   touch_swap_axes, touch_mirror_x, touch_mirror_y
//   has_color_panel

// Drawing
int fbink_fill_rect_gray(int fd, const FBInkRect*, FBInkConfig*, uint8_t, bool, bool);
int fbink_print(int fd, const char* string, const FBInkConfig*);
int fbink_print_ot(int fd, const char* string, const FBInkOTConfig*, const FBInkConfig*, char*);
int fbink_refresh(int fd, uint32_t top, uint32_t left, uint32_t width, uint32_t height, const FBInkConfig*);
// FBInkConfig.wfm_mode: WFM_GC16, WFM_DU, WFM_A2, WFM_REAGL, etc.
// FBInkConfig.is_flashing: force full flash
```

### FBInk touch quirk flags
The `FBInkState` struct provides device-specific touch quirk flags that could replace or supplement the axis-span heuristic:
- `touch_swap_axes`: Device needs X/Y axis swap
- `touch_mirror_x`: Device needs X axis mirrored
- `touch_mirror_y`: Device needs Y axis mirrored
- `is_ntx_quirky_landscape`: NTX device with landscape-native panel

These flags come from FBInk's internal device database — much more reliable than guessing from axis ranges.
