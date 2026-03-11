# sKeets — Bug & Feature Gap Report

Fresh analysis of the entire codebase, cross-referenced against KOReader-base,
NickelHook, NickelMenu, and the Kobo-Reader build tree. Items already captured
in `rewrite-plan.md` are marked with *(plan)*.

---

## Critical Bugs

### B1. Hardcoded screen dimensions *(plan — Phase 0)*

**Files**: `fb.h`, `feed_view.cpp`, `thread_view.cpp`, `settings_view.cpp`,
`login_view.cpp`, `compose_view.cpp`, `input.cpp`

`FB_WIDTH=1072` / `FB_HEIGHT=1448` are compile-time constants used in every
view for layout, centering, and divider lines, but FBInk may report different
runtime values. On the Clara Colour specifically, `screen_width` can be 1448
and `screen_height` 1072 due to the NTX landscape-native panel.

Every file that references `FB_WIDTH` or `FB_HEIGHT` is affected:
- `feed_view.cpp`: `CONTENT_W`, `draw_topbar()`, `draw_bottombar()`, `draw_post()` divider, centering
- `thread_view.cpp`: `content_width_for_indent()`, `draw_topbar()`, divider, bottom bar
- `settings_view.cpp`: `ROW_W`, `logout_row_y()`, centering, version string
- `login_view.cpp`: `FIELD_X`, `EXIT_BTN`, button centering
- `compose_view.cpp`: `TEXT_AREA_W`, `POST_BTN_X`
- `input.cpp`: `should_swap_axes()`, `map_touch_point()` normalization target

### B2. Font cell size mismatch *(plan — Phase 0)*

**File**: `font.cpp`

`FONT_CHAR_W=8` / `FONT_CHAR_H=16` don't account for FBInk's `fontsize_mult`
scaling on high-DPI screens. Text renders at wrong positions relative to
layout expectations. Full analysis in `rewrite-plan.md` Section 0 Bug 2.

### B3. Touch coordinate normalization *(plan — Phase 0)*

**File**: `input.cpp`

Touch points normalized to `[0, FB_WIDTH)` / `[0, FB_HEIGHT)` instead of
actual screen dimensions. All hit tests miss their targets when the real
screen size differs from the hardcoded constants.

### B4. No mid-session JWT refresh — HIGH SEVERITY

**Files**: `app.cpp`, `atproto_client.cpp`

ATProto access JWTs expire after ~2 hours. sKeets only calls `resumeSession()`
once at startup (in `app_init()`). After the token expires, every API call
will fail with an auth error.

`app_ensure_auth()` calls `resumeSession()` but only when `hasSession()`
returns false. If the SDK's internal session object still exists but has an
expired JWT, `hasSession()` may return true while API calls fail.

**Impact**: After ~2 hours of use, the feed stops refreshing and thread
loading fails. The user must exit and relaunch.

**Fix**: Implement proactive token refresh before expiry:
1. Track JWT expiry time (decode the JWT to extract `exp`, or just track
   `last_refresh_time + 90 minutes`)
2. In `app_ensure_auth()`, also check if the token is near-expiry and call
   `mClient->refreshSession()` (the SDK supports this)
3. On any 401/ExpiredToken error from an API call, attempt refresh before
   falling through to the auth error path

### B5. Thread reply flattening is only one level deep

**File**: `atproto_client.cpp` lines 211-219

`getPostThread()` iterates `(*tvp)->mReplies` but does not recursively process
each reply's own `mReplies`. The ATProto SDK returns a full tree
(`ThreadViewPost` has its own `mReplies` vector), but sKeets only captures the
first level of children. All deeper comment threads are silently lost.

**Code**:
```cpp
for (auto& reply : (*tvp)->mReplies) {
    // Only processes top-level replies
    auto* rtvp = std::get_if<...>(&reply->mPost);
    if (rtvp && *rtvp && (*rtvp)->mPost)
        root.replies.push_back(std::make_shared<Post>(convertPostView((*rtvp)->mPost)));
    // (*rtvp)->mReplies is never processed — nested replies are dropped!
}
```

**Fix**: Add a recursive helper:
```cpp
static void flattenReplies(Post& parent, const ATProto::AppBskyFeed::ThreadViewPost& tvp) {
    for (auto& reply : tvp.mReplies) {
        if (!reply || reply->mType != THREAD_VIEW_POST) continue;
        auto* rtvp = std::get_if<ThreadViewPost::SharedPtr>(&reply->mPost);
        if (!rtvp || !*rtvp || !(*rtvp)->mPost) continue;
        auto child = std::make_shared<Post>(convertPostView((*rtvp)->mPost));
        flattenReplies(*child, **rtvp);  // recurse
        parent.replies.push_back(std::move(child));
    }
}
```

### B6. ViewerState not extracted from posts

**File**: `atproto_client.cpp`

`convertPostView()` never reads `pv->mViewer->mLike` or `pv->mViewer->mRepost`.
This means even after like/repost buttons are added, the app won't know if
the user has already liked/reposted a post. Already covered in the plan but
the current code is buggy even for display purposes — the heart icon should
show filled if the user already liked.

---

## High-Severity Bugs

### B7. Unbounded disk image cache

**File**: `image.cpp`, `image_cache.cpp`

The disk cache under `$SKEETS_DATA_DIR/cache/` has no size limit and no
eviction. Every image URL writes a raw RGBA file (typically 20-200KB each).
After browsing a few hundred posts, this can consume hundreds of MB on the
Kobo's limited eMMC storage (usually 8-32GB total, shared with books).

**Fix**: Track total cache size; evict oldest files when a threshold is reached
(e.g., 50MB). Or use a simpler approach: on startup, delete cache files older
than N days.

### B8. In-memory image cache eviction is naive

**File**: `image_cache.cpp`

`evict_if_full()` removes the first non-loading entry it finds when the cache
hits 128 entries. This is not LRU — it evicts in hash-map iteration order,
which means frequently accessed images (like the user's own avatar) can be
evicted while stale images from old scrolls persist.

**Fix**: Add a `last_accessed` timestamp to `cache_entry_t` and evict the
oldest entry.

### B9. `font_draw_wrapped_fallback` line buffer overflow risk

**File**: `font.cpp`

```cpp
char line[512];
```

The word-wrap fallback builds lines in a 512-byte stack buffer. Very long
words (e.g., URLs like `https://example.com/very/long/path/...`) that exceed
512 bytes will write past the buffer. Since `memcpy(line + line_len, word, word_len)`
doesn't bounds-check against `sizeof(line)`, a single word longer than 512
bytes causes a stack buffer overflow.

**Fix**: Add a bounds check: `if (line_len + word_len + 1 > sizeof(line)) break;`

### B10. `font.cpp` static screen dimensions initialized backwards

**File**: `font.cpp` line 11-12

```cpp
static int s_screen_w = 1448;
static int s_screen_h = 1072;
```

Width and height are swapped compared to `FB_WIDTH=1072`, `FB_HEIGHT=1448`.
These are overwritten by `font_init()`, so they only matter if any font
function is called before `font_init()`. The risk is in `font_measure_wrapped()`
which uses `s_screen_w` to compute OT margins — if called before init, the
margin would be wildly wrong, potentially causing FBInk to render off-screen.

### B11. Timeline shows reposts without attribution

**File**: `atproto_client.cpp`

The ATProto timeline response includes repost "reasons" — when a post appears
because someone the user follows reposted it, the feed item includes
`reason.by` (the reposter's profile). `getTimeline()` ignores this entirely:

```cpp
for (auto& item : output->mFeed) {
    if (!item || !item->mPost) continue;
    feed.items.push_back(convertPostView(item->mPost));
    // item->mReason is never read — no "reposted by @user" attribution
}
```

The user sees reposted content but has no way to know it's a repost or who
reposted it.

**Fix**: Add `reposted_by` field to `Bsky::Post`; extract from
`item->mReason` when it's a repost reason.

---

## Medium-Severity Bugs

### B12. No touch axis mirror/swap per device model

**File**: `input.cpp`

`should_swap_axes()` guesses from axis spans. KOReader maintains explicit
per-model tables because:
- Newer MTK Kobos (Clara Colour, Libra Colour) need `mirror_y=true` instead
  of `mirror_x=true`
- The axis span heuristic can give wrong results when both axes have similar
  ranges

FBInk exposes `FBInkState.touch_swap_axes`, `touch_mirror_x`, `touch_mirror_y`
which come from its internal device database. sKeets should use these instead
of guessing.

### B13. No suspend/resume handling

**File**: none (missing entirely)

sKeets has no power management. The device stays fully awake while the app
runs, draining the battery. KOReader has a full suspend state machine:
screensaver → Wi-Fi off → frontlight off → sync → suspend-to-RAM.

At minimum, sKeets should:
1. Detect inactivity timeout → blank screen → low-power state
2. Handle power button press → sleep/wake cycle
3. MTK Kobos crash on suspend while charging — must skip suspend in that case

### B14. ~~No feed pagination / "load more"~~ — WONTFIX

**File**: `feed_view.cpp`

The 50-post batch from `getTimeline()` is sufficient for a scrolling session.
The existing Refresh button in the top bar loads a fresh batch. No pagination
needed.

### B15. `config_set_str` silently drops entries beyond 64

**File**: `config.cpp`

```cpp
if (cfg->count >= CONFIG_MAX_ENTRIES) return;
```

When the config has 64 entries, any new key is silently discarded with no
error or warning. With the dual image toggle additions (profile_images_enabled,
embed_images_enabled) plus existing keys, this limit could be hit.

### B16. `str_hash` collision risk for image cache

**File**: `str.cpp`, `image_cache.cpp`

The image cache keys URLs by djb2a hash (`unsigned long` = 32 or 64 bits).
Two different URLs that hash to the same value would cause one to serve the
other's image data. The probability is low but nonzero, especially with long
URLs that differ only in query parameters.

**Fix**: Store the full URL string alongside the hash for collision checking,
or use a stronger hash.

### B17. Full-screen refresh on every draw

**Files**: `feed_view.cpp`, `thread_view.cpp`, `settings_view.cpp`, `login_view.cpp`

Every view's `_draw()` function calls `fb_refresh_full()` which does a
GC16 flashing refresh. This means:
- Every scroll-drag triggers a full flash (annoying e-ink flicker)
- Toggling a like should only need a partial DU on the stats line, not a
  full-screen flash
- The image cache redraw path (`image_cache_redraw_needed()`) triggers a
  full flash just because one avatar finished loading

### B18. `app_switch_view` redraws for image cache but doesn't check view state

**File**: `app.cpp`

```cpp
if (image_cache_redraw_needed())
    app_switch_view(state, state->current_view);
```

This re-enters the current view (clearing its state — e.g., `thread_scroll` reset
in the `VIEW_THREAD` case), merely because an image finished downloading. In
thread view, this resets scroll to 0 and reloads the thread from the network.

**Fix**: Call the view's `_draw()` directly without reinitializing state.
For thread view specifically, `app_switch_view` resets `thread_scroll = 0`
and calls `thread_view_load()` — catastrophic for just a redraw.

---

## Feature Gaps (not bugs, but missing for v2)

### G1. No like/repost interaction *(plan — Phase 3)*

Stats line is display-only in both feed and thread views. No tappable buttons.

### G2. No file-based login flow *(plan — Phase 2)*

Currently uses interactive text fields. Plan specifies login.txt approach.

### G3. Single image toggle instead of dual *(plan — Phase 3.5)*

`images_enabled` controls all images. Need separate `profile_images_enabled`
(default true) and `embed_images_enabled` (default false).

### G4. Compose view still present *(plan — Phase 1)*

Needs to be deleted. Thread view tap currently routes to compose.

### G5. No swipe gesture detection *(plan — Phase 4)*

Only tap is recognized. Feed scroll is implemented by tracking TOUCH_MOVE
deltas manually in the view, which works but is brittle.

### G6. No long-press detection *(plan — Phase 4)*

Not currently detected. Could be useful for confirming destructive actions.

### G7. No device model table for touch transforms *(plan — Phase 4)*

Relies on axis-span heuristic. Should use FBInk's `touch_swap_axes`,
`touch_mirror_x`, `touch_mirror_y` flags from `FBInkState`.

### G8. RGB565 color model *(plan — Phase 5)*

Unnecessary double conversion: RGB565 constants → `rgb565_to_gray()` → FBInk.
Should use 8-bit grayscale directly.

### G9. No dirty-region tracking *(plan — Phase 5)*

Every draw is full-screen. Could dramatically reduce flicker by only refreshing
changed regions.

### G10. No completion fencing after refresh *(plan — Phase 5)*

FBInk supports `fbink_wait_for_complete()` but sKeets doesn't call it.
Rapid refreshes during scroll can cause tearing or ghosting.

---

## Summary by Priority

| Priority | ID | Issue |
|----------|------|-------|
| CRITICAL | B1-B3 | Hardcoded dimensions/font/touch (plan Phase 0) |
| CRITICAL | B4 | No JWT refresh — app breaks after ~2 hours |
| CRITICAL | B5 | Thread replies only one level deep |
| CRITICAL | B6 | ViewerState not extracted (needed for like/repost) |
| HIGH | B7 | Unbounded disk cache |
| HIGH | B8 | Naive in-memory cache eviction |
| HIGH | B9 | Font line buffer overflow |
| HIGH | B11 | Reposts shown without attribution |
| HIGH | B18 | Image redraw resets view state (thread scroll, network reload) |
| MEDIUM | B10 | Font static dims initialized backwards |
| MEDIUM | B12 | No per-device touch transforms |
| MEDIUM | B13 | No suspend/resume |
| MEDIUM | B14 | ~~No feed pagination~~ — WONTFIX, refresh is sufficient |
| MEDIUM | B15 | Config entry limit silently drops keys |
| MEDIUM | B16 | Hash collision risk in image cache |
| MEDIUM | B17 | Full-screen flash on every draw |
