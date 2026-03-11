# sKeets v2 — Complete Rewrite Plan

## Scope changes

### Removed
- **Compose view** — no posting or replying
- **Login UI** — no on-device text entry for credentials
- **PDS URL field** — moved to credentials file

### Added
- **Like/unlike** on posts (feed and thread views)
- **Repost/unrepost** on posts (feed view only — not in thread/comments)
- **Credentials file** login flow (`login.txt` → auto-login → delete file → store tokens)
- **Dual image toggles** — profile images (avatars) on by default, embed images off by default

### Retained
- Feed timeline (scrollable, virtualized) — author, post time, content, rendered embeds
- Thread / comments view (tap to browse, like interaction only)
- Settings view (profile images toggle, embed images toggle, sign-out)
- Image loading & caching (avatars, post images)
- NickelMenu / KFMon launch path

---

## 0. Root cause: text/touch misalignment bug

The current build has a critical coordinate mismatch that breaks both text
rendering alignment and button interaction (e.g., the exit button is
untappable). There are **three independent bugs**, all related:

### Bug 1 — Hardcoded dimensions are swapped

[fb.h](src/ui/fb.h#L8):
```c
#define FB_WIDTH    1072
#define FB_HEIGHT   1448
```

But [font.cpp](src/ui/font.cpp#L11) initialises its own copies the other way:
```c
static int s_screen_w = 1448;
static int s_screen_h = 1072;
```

`font_init()` overwrites these with the actual values from FBInk
(`fb->width`, `fb->height`), so the stale defaults don't matter at runtime —
but the **`FB_WIDTH` / `FB_HEIGHT` constants** are used by every view for
layout calculations and by the input layer for touch coordinate mapping. If
FBInk reports the screen as 1448×1072 (landscape native, which it may
depending on boot rotation and the `is_ntx_quirky_landscape` quirk), then all
the view layout math uses the wrong axis for width vs height.

**The actual screen_width and screen_height from FBInk may not match 1072×1448.**
On the Clara Colour, FBInk's `screen_width` can be **1448** and
`screen_height` can be **1072** due to the NTX 16bpp landscape quirk (the
native framebuffer is landscape, and FBInk's `is_ntx_quirky_landscape` flag
compensates for rotation). The hardcoded constants assume portrait.

### Bug 2 — FBInk's `fbink_print` uses a cell grid, not pixel coordinates

The fallback text renderer in [font.cpp](src/ui/font.cpp#L37) does:
```c
cfg.row     = (short int)(y / FONT_CHAR_H);   // y / 16
cfg.col     = (short int)(x / FONT_CHAR_W);   // x / 8
cfg.hoffset = (short int)(x % FONT_CHAR_W);   // x % 8
cfg.voffset = (short int)(y % FONT_CHAR_H);   // y % 16
```

This divides pixel coordinates by `FONT_CHAR_W=8` / `FONT_CHAR_H=16` to get
a cell row/col, then uses the remainder as a sub-cell offset.

But FBInk's actual cell size is **not** 8×16 on a high-DPI screen. FBInk
reports the effective (scaled) cell dimensions via `FBInkState.font_w` and
`FBInkState.font_h`. On a 300 DPI screen with the VGA font, FBInk may apply
a `fontsize_mult` scaling factor, making the effective cell size something
like 16×32 or 24×48 — not 8×16.

When sKeets divides pixel position `y=340` by 16 to get `row=21` but FBInk's
actual row 21 is at `y = 21 × font_h` (where `font_h` could be 32 or 48),
the text lands at a completely different vertical position than the view
intended. The `hoffset`/`voffset` sub-cell offsets compound this because
they're calculated modulo the wrong cell size.

This is the primary cause of text appearing misaligned from where buttons
and hit zones expect it to be.

### Bug 3 — Touch coordinates map to wrong target dimensions

In [input.cpp](src/ui/input.cpp#L104), touch coordinates are normalized to:
```c
normalize_axis(raw_x, ..., device.swap_axes ? FB_HEIGHT - 1 : FB_WIDTH - 1);
normalize_axis(raw_y, ..., device.swap_axes ? FB_WIDTH - 1  : FB_HEIGHT - 1);
```

If the actual framebuffer is landscape (1448 wide) but `FB_WIDTH=1072`, touch
points are scaled to the wrong range. A tap at physical screen position
(700, 500) maps to a different logical coordinate than the views expect,
making all hit tests miss their targets.

### Fix

All three bugs share a single root cause: **hardcoded pixel dimensions and
font metrics that don't match the actual device state**.

The fix for the rewrite:

1. **Remove `FB_WIDTH` / `FB_HEIGHT` constants.** Use `fb->width` /
   `fb->height` everywhere (already stored from `fbink_get_state`).

2. **Query FBInk's actual font cell size at init.** After `fbink_init()` +
   `fbink_get_state()`, read `state.font_w` and `state.font_h` into the
   `fb_t` struct. Replace `FONT_CHAR_W` / `FONT_CHAR_H` with these runtime
   values.

3. **Fix `fbink_print` row/col calculation.** Use the real cell dimensions:
   ```c
   cfg.row     = (short int)(y / fb->font_h);
   cfg.col     = (short int)(x / fb->font_w);
   cfg.hoffset = (short int)(x % fb->font_w);
   cfg.voffset = (short int)(y % fb->font_h);
   ```

4. **Pass `fb->width` / `fb->height` to the input layer** instead of
   hardcoded constants, so touch coordinates are normalized to the actual
   screen dimensions.

5. **Update all view layout math** to use `fb->width` / `fb->height` and
   `fb->font_w` / `fb->font_h` instead of compile-time constants.

This fix is incorporated into Phase 5 (Framebuffer improvements) and Phase 1
(Scaffolding) of the implementation plan below, but it should realistically be
**the very first thing fixed** since nothing else can be properly tested until
the screen coordinates are correct.

---

## 1. Input layer — sKeets vs KOReader gap analysis

### Current sKeets input (`src/ui/input.cpp`, 260 lines)

| Aspect | Current | KOReader reference |
|---|---|---|
| Device discovery | `opendir("/dev/input")`, opens every `event*` | Same pattern, but opens specific known paths |
| Axis probing | `EVIOCGABS(ABS_MT_POSITION_X)` with `ABS_X` fallback | Same, plus per-device override tables |
| Axis swapping | Heuristic comparing axis spans to screen dims | Hardcoded per device model from table |
| Coordinate mapping | Linear `normalize_axis()` to `FB_WIDTH`/`FB_HEIGHT` | Same, but device-specific transforms for rotation |
| Grab | `EVIOCGRAB(1)` on open, release on close | KOReader also grabs unconditionally |
| Event dispatch | Single `poll()` over all fds, return first event | KOReader uses `select()` + batch buffer (256 events), drains entire queue per wakeup |
| Gesture recognition | `input_is_tap()` (Manhattan distance ≤ 30px) | Lua-side gesture detector (tap, hold, swipe, pinch, pan) |
| USB/charging events | Not handled | KOReader forks child to listen for kernel uevents |
| Timer events | Not handled | KOReader uses `timerfd` fds mixed into select set |
| Button mapping | POWER, NEXTSONG/104, PREVIOUSSONG/109 | Similar, plus device-specific alternate codes |

### Refactor plan

**Keep** the current single-poll model — it is adequate for sKeets' simple interaction model (tap, scroll-drag, buttons). KOReader's batch model is valuable when running a 60fps-striving reading app; sKeets does not need it.

**Changes needed:**

1. **Device-aware axis tables.** Replace the heuristic `should_swap_axes()` with a lookup keyed on device model from `device_kobo_model_string()`. The Clara Colour is the only target today, but a table is trivial and prevents future regressions. Falls back to the current heuristic for unknown models.

2. **Swipe gesture detection.** Add a `TOUCH_SWIPE_LEFT`, `TOUCH_SWIPE_RIGHT`, `TOUCH_SWIPE_UP`, `TOUCH_SWIPE_DOWN` to `touch_type_t`, detected from TOUCH_DOWN→TOUCH_UP displacement and velocity. This replaces the current approach where feed_view manually tracks `last_down` and computes `dy` inline.

3. **Long-press detection.** Add a `TOUCH_LONG_PRESS` event emitted when a TOUCH_DOWN has not moved beyond the tap threshold for >500ms. This enables "like on long-press" without polling from the view layer.

4. **USB plug events (stretch goal).** Fork a uevent listener child like KOReader's `generateFakeEvent()`. When USB is plugged in, inject a fake `INPUT_KEY` / `KEY_USB_IN` so the app can gracefully exit rather than freezing when the user connects USB. Not critical for v2 MVP.

**Don't copy from KOReader:**
- Batch event buffering (unnecessary — our event rate is low)
- timerfd integration (our timeout poll is sufficient)
- Multi-touch slot tracking (Kobo Clara Colour is single-touch)

---

## 2. Framebuffer layer — sKeets vs KOReader gap analysis

### Current sKeets FB (`src/ui/fb.cpp`, 240 lines via FBInk)

| Aspect | Current | KOReader reference |
|---|---|---|
| Abstraction | FBInk library handles everything | Raw mmap'd framebuffer + manual ioctl |
| Init | `fbink_open()` / `fbink_init()` | `open("/dev/fb0")`, `FBIOGET_VSCREENINFO`, mmap |
| Drawing | `fbink_fill_rect_gray()`, `fbink_print_raw_data()` | Direct pixel writes to mmap'd buffer via Blitbuffer |
| Text rendering | FBInk's OT font engine + built-in bitmap fallback | Freetype via LuaJIT FFI |
| Refresh | `fbink_refresh()` with `WFM_DU`, `WFM_A2`, or default GC16 | Direct `ioctl(MXCFB_SEND_UPDATE)` with device-family-specific structs, waveform selection logic, EPDC wakeup, marker tracking |
| Color model | RGB565 constants, converted to grayscale for FBInk | Blitbuffer supports Y8, RGB32, RGB565 natively |
| Rotation | Not handled (device is always portrait) | Full rotation support via FBIOPUT_VSCREENINFO |
| Night mode | Not handled | HW flag on FSCREENINFO grayscale field |
| Dithering | Not handled | HW dithering flag on update struct |
| EPDC wakeup | Implicit (FBInk handles it) | Explicit write to `/sys/class/graphics/fb0/power_state` on NXP |

### Refactor plan

**Keep FBInk as the rendering backend.** FBInk already abstracts the device-specific ioctl gymnastics that KOReader handles manually. Re-implementing those ioctls would require supporting every Kobo SoC variant (NXP, MTK, Allwinner) — that's exactly what FBInk exists for.

**Changes needed:**

1. **Add `fb_refresh_gc16_partial()`** — a partial refresh with GC16 waveform instead of DU. DU is fast but monochrome-only; when images are enabled, GC16 partial is needed for grayscale fidelity in the refreshed region. FBInk supports this: set `cfg.wfm_mode = WFM_GC16` with a non-zero region.

2. **Add `fb_wait_for_complete()`** — wraps `fbink_wait_for_complete()`. Currently refreshes are fire-and-forget. KOReader's marker-based wait system prevents tearing during rapid scrolls. sKeets should wait before the next draw after a full refresh to prevent ghosting.

3. **Remove RGB565 color model.** FBInk renders to the device's native pixel format (usually Y8 or RGB32 depending on device). The current `uint16_t` RGB565 color constants and `rgb565_to_gray()` conversion are an unnecessary intermediary. Replace with a simple 8-bit grayscale palette:
   ```c
   #define COLOR_BLACK  0x00
   #define COLOR_WHITE  0xFF
   #define COLOR_GRAY   0x80
   #define COLOR_LGRAY  0xC0
   #define COLOR_DGRAY  0x40
   ```
   All drawing functions already convert to grayscale before calling FBInk — this just removes the double conversion.

4. **Separate refresh policy from drawing.** Currently each view calls `fb_refresh_full()` after drawing. Extract a `fb_mark_dirty(region)` / `fb_flush()` pattern so the main loop controls when and how refreshes happen. This prevents redundant full-screen refreshes when only a small tap target changed (e.g., toggling a like).

**Don't copy from KOReader:**
- Raw mmap framebuffer access (FBInk is better maintained for cross-device support)
- Rotation handling (sKeets is portrait-only)
- Night mode (out of scope)
- Device-family ioctl dispatch tables (FBInk encapsulates this)

---

## 3. New architecture

### File structure

```
src/
├── main.cpp                     Entry point (unchanged shape)
├── app.h / app.cpp              State machine & event loop
├── atproto/
│   ├── atproto.h                Data structures (add viewer_like, viewer_repost fields)
│   ├── atproto_client.h/cpp     SDK wrapper (add like/unlike, repost/unrepost; remove createPost)
├── ui/
│   ├── fb.h / fb.cpp            Framebuffer (grayscale palette, dirty regions, GC16 partial)
│   ├── font.h / font.cpp        Text rendering (unchanged)
│   ├── input.h / input.cpp      Input (add swipe, long-press detection)
│   ├── views.h                  View enum (remove VIEW_COMPOSE, VIEW_LOGIN; add VIEW_AUTH_WAIT)
│   ├── auth_view.h / auth_view.cpp    "Waiting for login.txt" / "Signing in..." splash
│   ├── feed_view.h / feed_view.cpp    Timeline (add like button, remove "+ Post")
│   ├── thread_view.h / thread_view.cpp  Comments (read-only, add like, remove reply)
│   └── settings_view.h / settings_view.cpp  (dual image toggles: profile + embed)
└── util/
    ├── config.h / config.cpp    INI config (unchanged)
    ├── paths.h / paths.cpp      Paths (add login_txt_path)
    ├── device.h / device.cpp    Device detection (unchanged)
    ├── image.h / image.cpp      Image download (unchanged)
    ├── image_cache.h / image_cache.cpp  Async cache (unchanged)
    └── str.h / str.cpp          String utils (unchanged)
```

### Removed files
- `src/ui/login_view.h` / `src/ui/login_view.cpp` — replaced by `auth_view`
- `src/ui/compose_view.h` / `src/ui/compose_view.cpp` — removed entirely

### View flow

```
┌─────────────────┐     login.txt exists     ┌──────────────┐
│   AUTH_WAIT      │ ──────────────────────►  │  AUTH_WAIT    │
│ "login.txt       │     (auto-detected)      │ "Signing in…" │
│  is missing"     │                          └──────┬───────┘
└────────┬─────────┘                                 │
         │                                    success│  failure
         │ tokens exist in config.ini                │    │
         │ (resumeSession succeeds)                  ▼    ▼
         │                               ┌───────────────────┐
         ▼                               │    AUTH_WAIT       │
┌─────────────┐                          │ "Login failed:     │
│   FEED      │ ◄────────────────────────│  <error>.          │
│  Timeline   │                          │  Update login.txt  │
└──────┬──────┘                          │  and Retry"        │
       │                                 └───────────────────┘
       │ tap post
       ▼
┌──────────────┐
│   THREAD     │  read-only comments, like
│   Comments   │
└──────┬───────┘
       │ < Back
       ▼
┌──────────────┐
│   SETTINGS   │  profile/embed image toggles, sign-out
└──────────────┘
```

### Event loop changes

```cpp
void app_run(app_state_t *state) {
    app_switch_view(state, state->current_view);
    input_event_t ev{};
    while (state->running) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (image_cache_redraw_needed())
            app_switch_view(state, state->current_view);

        if (!state->input) { nanosleep(...); continue; }

        bool got_ev = input_poll(state->input, &ev, 100);
        if (!got_ev) continue;

        if (ev.type == INPUT_KEY && ev.key == KEY_POWER) {
            state->running = false;
            break;
        }

        switch (state->current_view) {
            case VIEW_AUTH_WAIT: auth_view_handle(state, &ev);     break;
            case VIEW_FEED:     feed_view_handle(state, &ev);      break;
            case VIEW_THREAD:   thread_view_handle(state, &ev);    break;
            case VIEW_SETTINGS: settings_view_handle(state, &ev);  break;
        }
    }
}
```

---

## 4. Credentials file login flow

### File format

`/mnt/onboard/.adds/sKeets/login.txt` — plain text, one field per line:

```
handle=you.bsky.social
password=xxxx-xxxx-xxxx-xxxx
pds_url=https://pds.example.com
appview=https://api.bsky.app
```

- `handle` — required (Bluesky handle or email)
- `password` — required (app password)
- `pds_url` — optional (blank or absent = default `https://bsky.social`)
- `appview` — optional (blank or absent = default `https://api.bsky.app`). The AppView endpoint used for feed/thread queries. Only relevant for users running a custom relay or AppView.

### Flow

```
app_init():
  1. Open config.ini
  2. If saved session exists (handle + access_jwt + did):
       → resumeSession()
       → if success: current_view = VIEW_FEED, done
       → if failure: fall through to step 3
  3. Check if login.txt exists:
       → if yes: read credentials, attempt createSession()
         → if success:
             - save tokens + appview to config.ini
             - delete login.txt (unlink())
             - current_view = VIEW_FEED
         → if failure:
             - delete login.txt (security: don't keep failed passwords on disk)
             - current_view = VIEW_AUTH_WAIT with error message
       → if no: current_view = VIEW_AUTH_WAIT with instructions
```

### auth_view screen

A simple splash with centered text. There is no Retry button — modifying
`login.txt` requires connecting the device to a computer, so the user must
restart sKeets after creating/editing the file.

```
┌────────────────────────────────────┐
│                                    │
│         sKeets for Kobo            │
│                                    │
│   To sign in:                      │
│                                    │
│   1. Connect your Kobo via USB     │
│   2. Create the file:              │
│      .adds/sKeets/login.txt        │
│   3. Add your credentials:         │
│      handle=you.bsky.social        │
│      password=xxxx-xxxx-xxxx-xxxx  │
│   4. Restart sKeets                │
│                                    │
│   Optional settings in login.txt:  │
│      pds_url=https://pds.example   │
│      appview=https://my.appview    │
│                                    │
│   <error message if login failed>  │
│                                    │
│          ┌─────────┐               │
│          │  Exit   │               │
│          └─────────┘               │
│                                    │
└────────────────────────────────────┘
```

The auth_view is static — there is only an **Exit** button. The login
attempt happens automatically in `app_init()` when `login.txt` is found,
before the view is ever drawn. If login fails, the error message is
displayed and the user must fix the file and relaunch.

### Security considerations

- `login.txt` is deleted immediately after reading, whether login succeeds or fails
- The file lives on user-accessible storage (`/mnt/onboard`), same as the existing config.ini approach — this is not a regression
- Tokens in config.ini are the same security model as v1 (documented "use app passwords")
- File contents are read into stack buffers, not heap-allocated, and zeroed after use

---

## 5. ATProto client changes

### Remove

```cpp
// DELETE: createPost() — no more posting
void createPost(const std::string& text,
                const std::optional<std::string>& reply_parent_uri, ...);
```

### Add

```cpp
// Like a post. Returns the like record's AT-URI and CID.
void likePost(const std::string& uri, const std::string& cid,
              const PostCreatedCb& successCb, const ErrorCb& errorCb);

// Un-like a post. Takes the like record's AT-URI (from viewer state).
void unlikePost(const std::string& likeUri,
                const SuccessCb& successCb, const ErrorCb& errorCb);

// Repost a post. Returns the repost record's AT-URI and CID.
void repostPost(const std::string& uri, const std::string& cid,
                const PostCreatedCb& successCb, const ErrorCb& errorCb);

// Un-repost a post. Takes the repost record's AT-URI (from viewer state).
void unrepostPost(const std::string& repostUri,
                  const SuccessCb& successCb, const ErrorCb& errorCb);
```

Implementation uses the SDK's `PostMaster::repost()` and `PostMaster::undo()`:

```cpp
void AtprotoClient::repostPost(const std::string& uri, const std::string& cid,
                                const PostCreatedCb& successCb, const ErrorCb& errorCb) {
    ATProto::PostMaster pm(*mClient);
    QEventLoop loop;
    pm.repost(QString::fromStdString(uri), QString::fromStdString(cid),
              {}, {},
              [&loop, successCb](const QString& repostUri, const QString& repostCid) {
                  successCb(repostUri.toStdString(), repostCid.toStdString());
                  loop.quit();
              },
              [&loop, errorCb](const QString& err, const QString& msg) {
                  errorCb((err + ": " + msg).toStdString());
                  loop.quit();
              });
    loop.exec();
}

void AtprotoClient::unrepostPost(const std::string& repostUri,
                                  const SuccessCb& successCb, const ErrorCb& errorCb) {
    ATProto::PostMaster pm(*mClient);
    QEventLoop loop;
    pm.undo(QString::fromStdString(repostUri),
            [&loop, successCb]() { successCb(); loop.quit(); },
            [&loop, errorCb](const QString& err, const QString& msg) {
                errorCb((err + ": " + msg).toStdString());
                loop.quit();
            });
    loop.exec();
}
```

Implementation uses the SDK's `PostMaster::like()` and `PostMaster::undo()`:

```cpp
void AtprotoClient::likePost(const std::string& uri, const std::string& cid,
                              const PostCreatedCb& successCb, const ErrorCb& errorCb) {
    ATProto::PostMaster pm(*mClient);
    QEventLoop loop;
    pm.like(QString::fromStdString(uri), QString::fromStdString(cid),
            {}, {},  // no via URI/CID
            [&loop, successCb](const QString& likeUri, const QString& likeCid) {
                successCb(likeUri.toStdString(), likeCid.toStdString());
                loop.quit();
            },
            [&loop, errorCb](const QString& err, const QString& msg) {
                errorCb((err + ": " + msg).toStdString());
                loop.quit();
            });
    loop.exec();
}

void AtprotoClient::unlikePost(const std::string& likeUri,
                                const SuccessCb& successCb, const ErrorCb& errorCb) {
    ATProto::PostMaster pm(*mClient);
    QEventLoop loop;
    pm.undo(QString::fromStdString(likeUri),
            [&loop, successCb]() { successCb(); loop.quit(); },
            [&loop, errorCb](const QString& err, const QString& msg) {
                errorCb((err + ": " + msg).toStdString());
                loop.quit();
            });
    loop.exec();
}
```

### Data model changes (`atproto.h`)

```cpp
struct Post {
    // ... existing fields ...
    std::string viewer_like;    // AT-URI of current user's like record, empty = not liked
    std::string viewer_repost;  // AT-URI of current user's repost record, empty = not reposted
    // Remove: reply_parent_uri, reply_parent_cid, reply_root_uri, reply_root_cid
    //         (only needed for composing replies)
};
```

In `convertPostView()`, extract viewer state for both like and repost:

```cpp
if (pv->mViewer) {
    if (pv->mViewer->mLike)
        p.viewer_like = pv->mViewer->mLike->toStdString();
    if (pv->mViewer->mRepost)
        p.viewer_repost = pv->mViewer->mRepost->toStdString();
}
```

---

## 6. Feed view changes

### Top bar

```
Before: [Refresh]     Home     [+ Post]  [Settings]
After:  [Refresh]     Home               [Settings]
```

Remove the "+ Post" button and its hit zone.

### Post card — like interaction

Each post card gains a tappable heart icon in the stats line:

```
Before: ♡ 12  ↩ 3  ⟳ 1
After:  [♡] 12  💬 3  [⟳] 1        (♡ = like, ⟳ = repost — both tappable)
         ^               ^
         like target     repost target
```

When tapped (like):
1. Optimistically toggle the icon (filled ♥ / outline ♡) and ±1 the count
2. Partial DU refresh on just the stats line region
3. Fire async `likePost()` / `unlikePost()` in the background
4. On failure, revert the optimistic state and refresh

When tapped (repost):
1. Optimistically toggle the icon (filled ⟳ / outline ⟳) and ±1 the count
2. Partial DU refresh on just the stats line region
3. Fire async `repostPost()` / `unrepostPost()` in the background
4. On failure, revert the optimistic state and refresh

### Tap routing

```
Tap on top bar area:
  x < 120        → Refresh
  x > FB_WIDTH-120 → Settings
  (remove + Post zone)

Tap on post body  → open thread (VIEW_THREAD)
Tap on ♡ region   → toggle like
Tap on ⟳ region   → toggle repost
```

The ♡ and ⟳ hit zones are 44×44px areas around their icons (meets `UI_MIN_TAP`).

---

## 7. Thread view changes

### Top bar

```
Before: [< Back]     Thread     [Reply]
After:  [< Back]     Thread
```

Remove the "Reply" button.

### Post interaction

- Tapping a reply post **no longer opens compose** — it does nothing (or could open the sub-thread in the future)
- The ♡ like button is tappable on every post in the thread, same as feed view
- Repost is **not tappable** in thread view — comments only support like interaction
- The stats line shows `[♡] N  💬 N  ⟳ N` (♡ tappable, ⟳ display-only)

---

## 8. Implementation order

### Phase 0 — Fix coordinate misalignment (CRITICAL — do first)

1. In `fb_open()`, after `fbink_get_state()`, store `state.font_w` and `state.font_h` into `fb_t`
2. Remove `FB_WIDTH` / `FB_HEIGHT` from `fb.h`; replace all uses with `fb->width` / `fb->height`
3. Remove `FONT_CHAR_W` / `FONT_CHAR_H` from `font.h`; replace with `fb->font_w` / `fb->font_h` (passed via `font_init()`)
4. Fix `font_draw_string_fallback()` to divide by actual cell dimensions
5. Update `input_open()` to accept screen dimensions from `fb_t` instead of using `FB_WIDTH`/`FB_HEIGHT`
6. Build, deploy, verify text renders at correct positions and exit button responds to taps

### Phase 1 — Scaffolding (no behavior changes)

1. Remove `compose_view.h/cpp` from source tree and CMakeLists.txt
2. Remove `VIEW_COMPOSE` from `app_view_t` enum
3. Remove compose-related fields from `app_state_t` (`compose_reply_*`, `compose_root_*`)
4. Remove `createPost()` from `AtprotoClient`
5. Rename `login_view` → `auth_view`, change to display instructions splash
6. Add `VIEW_AUTH_WAIT` to enum, remove `VIEW_LOGIN`
7. Add `skeets_login_txt_path()` to `paths.h`
8. Build and verify everything compiles

### Phase 2 — Credential file login

1. Implement `auth_view_draw()` — instructions splash with Exit button only (no Retry)
2. Implement `auth_view_handle()` — Exit button handler
3. Update `app_init()` — replace config-based prefill logic with login.txt flow; parse optional `pds_url` and `appview` fields; save `appview` to config.ini on success
4. Add `appview` field to `Bsky::Session` and `AtprotoClient`; use it as the endpoint for feed/thread queries (default `https://api.bsky.app`)
5. Test: place login.txt, launch, verify login → tokens saved → file deleted

### Phase 3 — Like and repost support

1. Add `viewer_like` and `viewer_repost` fields to `Bsky::Post`
2. Extract `mViewer->mLike` and `mViewer->mRepost` in `convertPostView()`
3. Add `likePost()` / `unlikePost()` to `AtprotoClient`
4. Add `repostPost()` / `unrepostPost()` to `AtprotoClient`
5. Update `feed_view` stats line — tappable ♡ and ⟳ with hit tests
6. Implement optimistic toggle + async API call for both like and repost
7. Update `thread_view` stats line — tappable ♡ only (⟳ display-only)
8. Remove "Reply" button from thread top bar
9. Remove post-tap → compose navigation from thread view

### Phase 3.5 — Settings view: dual image toggles

1. Replace single `images_enabled` field in `app_state_t` with `profile_images_enabled` (default: **true**) and `embed_images_enabled` (default: **false**)
2. Update `settings_view` to show two toggle rows: "Profile images" and "Embed images"
3. Update config load/save to persist both values
4. Update `feed_view` and `thread_view` to check `profile_images_enabled` for avatars and `embed_images_enabled` for post image/external embeds
5. Update `image_cache` usage to respect the correct toggle per image type

### Phase 4 — Input improvements

1. Add `TOUCH_SWIPE_*` events to input layer
2. Add long-press detection (`TOUCH_LONG_PRESS`)
3. Replace `feed_view_handle` manual scroll tracking with swipe events
4. Use FBInk's `touch_swap_axes` / `touch_mirror_x` / `touch_mirror_y` from `FBInkState` instead of axis-span heuristic; keep heuristic as fallback for unknown devices

### Phase 5 — Framebuffer & reliability improvements

1. Replace RGB565 color model with 8-bit grayscale
2. Add `fb_refresh_gc16_partial()` for image-region updates
3. Add `fb_wait_for_complete()` after full refreshes
4. Add dirty-region tracking (`fb_mark_dirty()` / `fb_flush()`)
5. Fix `app_switch_view` image-cache redraw path — call view `_draw()` directly instead of reinitializing view state (currently resets thread_scroll and reloads thread from network)
6. Reduce full-screen flashing refreshes — use partial DU for scroll, stats line changes, and image arrivals

### Phase 5.5 — Bugs discovered in fresh analysis (see `bug-gap-report.md`)

1. **JWT refresh** — Implement proactive token refresh before ~2h expiry; add retry-on-401 logic in API call wrappers
2. **Thread reply recursion** — Replace single-level reply iteration in `getPostThread()` with recursive `flattenReplies()` helper that descends into each reply's `mReplies`
3. **Repost attribution** — Extract `item->mReason` from timeline feed items; add `reposted_by` field to `Bsky::Post`; display "⟳ reposted by @handle" above reposted posts
4. **Font line buffer overflow** — Add bounds check in `font_draw_wrapped_fallback()`: `if (line_len + word_len + 1 > sizeof(line)) break;`
5. **Disk cache size limit** — Track total cache size; evict oldest files when threshold reached (50MB); or delete cache files older than 7 days on startup
6. **In-memory cache LRU** — Add `last_accessed` timestamp to `cache_entry_t`; evict least recently used entry instead of arbitrary first
7. ~~**Feed pagination**~~ — Removed: the existing Refresh button and 50-post batch size is sufficient. Users scroll through the current batch and tap Refresh to load new posts.

### Phase 6 — Cleanup

1. Update README.md (remove compose/reply references, update login docs)
2. Update docs/plugin-port.md if needed
3. Remove stale comments referencing removed features
4. Docker build + on-device test

---

## 9. Files deleted in rewrite

| File | Reason |
|---|---|
| `src/ui/compose_view.h` | Feature removed |
| `src/ui/compose_view.cpp` | Feature removed |
| `src/ui/login_view.h` | Replaced by auth_view |
| `src/ui/login_view.cpp` | Replaced by auth_view |

## 10. Files created in rewrite

| File | Purpose |
|---|---|
| `src/ui/auth_view.h` | "No credentials" instructions splash |
| `src/ui/auth_view.cpp` | Static instructions display, error message, Exit button |
