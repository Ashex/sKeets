# Plan: sKeets v2 Rewrite Implementation

## TL;DR

Execute the rewrite plan (docs/rewrite-plan.md) and resolve all 17 actionable bugs from the bug-gap report (docs/bug-gap-report.md) across 8 phases. Phase 0 (coordinate fix) unblocks everything; Phases 1-3.5 deliver core features; Phases 4-5.5 harden input/rendering/reliability; Phase 6 cleans up. The ~3,800-line codebase (35 files) touches every source file — the approach is systematic file-by-file modification, not a full rewrite from scratch.

## Dependency Graph

```
Phase 0 (coordinates)
  ├── Phase 1 (scaffolding) 
  │     ├── Phase 2 (credential login)
  │     ├── Phase 3 (like/repost) ──── depends on Phase 0 for correct hit zones
  │     └── Phase 3.5 (dual toggles) ── parallel with Phase 3
  ├── Phase 4 (input) ── parallel with Phases 2-3.5
  └── Phase 5 (framebuffer) ── parallel with Phase 3+
        └── Phase 5.5 (bug fixes) ── partially parallel with Phase 5
              └── Phase 6 (cleanup) ── last
```

---

## Phase 0 — Fix Coordinate Misalignment (B1, B2, B3, B10)

*Unblocks all other phases. Nothing can be tested until this is done.*

**Steps:**
1. In `fb_open()` (fb.cpp), after `fbink_get_state()`, store `state.font_w` and `state.font_h` into `fb_t` struct — add two new fields to `fb_t` in fb.h
2. Remove `FB_WIDTH` / `FB_HEIGHT` defines from fb.h
3. Remove `FONT_CHAR_W` / `FONT_CHAR_H` defines from font.h
4. Remove `s_screen_w` / `s_screen_h` statics from font.cpp — replace with values passed through `font_init(fb_t*)`
5. Fix `font_draw_string_fallback()` (font.cpp ~L37): divide by `fb->font_h` / `fb->font_w` instead of hardcoded 16/8
6. Update `normalize_axis()` calls in input.cpp (~L104) — accept `fb->width` / `fb->height` instead of `FB_WIDTH` / `FB_HEIGHT`; pass fb dims through `input_open()` or store in `input_state_t`
7. Update every layout calculation in feed_view.cpp, thread_view.cpp, settings_view.cpp, login_view.cpp, compose_view.cpp — replace `FB_WIDTH`/`FB_HEIGHT` with `state->fb->width`/`state->fb->height` (use find-replace, then verify each site)
8. Update views.h shared constants that reference screen dims (e.g., `CONTENT_W`)

**Files modified:** fb.h, fb.cpp, font.h, font.cpp, input.h, input.cpp, views.h, feed_view.cpp, thread_view.cpp, settings_view.cpp, login_view.cpp, compose_view.cpp

**Verification:**
1. `./docker-build.sh` — must compile cleanly
2. `grep -rn 'FB_WIDTH\|FB_HEIGHT\|FONT_CHAR_W\|FONT_CHAR_H' src/` — must return zero matches
3. Deploy to device: verify text renders at correct positions, exit button responds to taps, feed scrolling works

---

## Phase 1 — Scaffolding (G4)

*Structural cleanup — no behavior changes.*

**Steps:**
1. Delete `src/ui/compose_view.h` and `src/ui/compose_view.cpp`
2. Remove compose source files from CMakeLists.txt `add_executable()` list
3. Remove `VIEW_COMPOSE` from `app_view_t` enum in views.h
4. Remove compose-related fields from `app_state_t` in app.h (`compose_reply_*`, `compose_root_*`, any compose state)
5. Remove `case VIEW_COMPOSE:` from `app_switch_view()` and event dispatch in app.cpp
6. Remove `createPost()` declaration from atproto_client.h and implementation from atproto_client.cpp
7. Rename `login_view.h/cpp` → `auth_view.h/cpp` (file rename + update CMakeLists.txt + all `#include` directives)
8. Replace `VIEW_LOGIN` with `VIEW_AUTH_WAIT` in views.h enum
9. Update all references to `VIEW_LOGIN` across app.cpp, app.h
10. Add `skeets_login_txt_path()` to paths.h/paths.cpp — returns `$SKEETS_DATA_DIR/login.txt`
11. Remove "+ Post" button hit zone from feed_view.cpp top bar
12. Remove "Reply" button hit zone from thread_view.cpp top bar
13. Remove tap-on-reply → compose navigation from thread_view.cpp

**Files modified:** CMakeLists.txt, views.h, app.h, app.cpp, atproto_client.h, atproto_client.cpp, feed_view.cpp, thread_view.cpp, paths.h, paths.cpp  
**Files deleted:** compose_view.h, compose_view.cpp  
**Files renamed:** login_view.h → auth_view.h, login_view.cpp → auth_view.cpp

**Verification:**
1. `./docker-build.sh` — clean compile
2. `grep -rn 'compose\|VIEW_COMPOSE\|VIEW_LOGIN\|createPost\|login_view' src/` — zero matches (except comments)
3. Verify `VIEW_AUTH_WAIT` appears in enum and all switch statements

---

## Phase 2 — Credential File Login (G2, B15 partial)

*Depends on Phase 1 (auth_view exists).*

**Steps:**
1. Rewrite `auth_view_draw()` — static instructions splash per wireframe in rewrite-plan.md Section 4: numbered steps, optional settings shown, error message area, Exit button only
2. Implement `auth_view_handle()` — Exit button tap → `state->running = false`
3. Implement `login_txt_read()` helper in auth_view.cpp (or a new `login.cpp` util):
   - Open `skeets_login_txt_path()`
   - Parse key=value lines: `handle`, `password` (required), `pds_url` (optional, default `https://bsky.social`), `appview` (optional, default `https://api.bsky.app`)
   - Return struct with parsed fields
4. Update `app_init()` in app.cpp — replace interactive login logic with:
   - Check saved session → `resumeSession()` → success → `VIEW_FEED`
   - Check `login.txt` exists → read → `createSession()` → success → save tokens + appview to config → delete file → `VIEW_FEED`
   - Login failure → delete login.txt → `VIEW_AUTH_WAIT` with error
   - No login.txt → `VIEW_AUTH_WAIT` with instructions
5. Add `appview` field to session storage (`atproto.h` Session struct or config key)
6. Use `appview` as endpoint for feed/thread API calls in atproto_client.cpp (*depends on SDK's endpoint configuration — verify how `ATProto::Client` sets its target host*)
7. **B15 partial**: While touching config.cpp, bump `CONFIG_MAX_ENTRIES` from 64 to 128 or make it dynamic

**Files modified:** auth_view.h, auth_view.cpp (rewritten from login_view), app.h, app.cpp, atproto.h, atproto_client.h, atproto_client.cpp, config.cpp, paths.h, paths.cpp

**Verification:**
1. Place valid login.txt on device → launch → tokens saved → file deleted → feed loads
2. Place invalid login.txt → launch → file deleted → auth_view shows error → Exit works
3. No login.txt → auth_view shows instructions → Exit works
4. Saved session → launch → feed loads directly (no login.txt needed)
5. Verify appview config persists across restarts

---

## Phase 3 — Like & Repost Interaction (G1, B6)

*Depends on Phase 1 (compose removed). Needs Phase 0 for correct hit zones.*

**Steps:**
1. Add `viewer_like` and `viewer_repost` `std::string` fields to `Bsky::Post` in atproto.h
2. In `convertPostView()` (atproto_client.cpp), extract `pv->mViewer->mLike` and `pv->mViewer->mRepost` → B6 fix
3. Add `likePost()` / `unlikePost()` to AtprotoClient using `PostMaster::like()` / `PostMaster::undo()` per code in rewrite-plan.md Section 5
4. Add `repostPost()` / `unrepostPost()` to AtprotoClient using `PostMaster::repost()` / `PostMaster::undo()` per code in rewrite-plan.md Section 5
5. Update feed_view.cpp stats line rendering — draw tappable `[♡]` and `[⟳]` icons with filled/outline state based on `viewer_like`/`viewer_repost`
6. Add hit zone detection for ♡ and ⟳ in `feed_view_handle()` — 44×44px tap targets (≥ `UI_MIN_TAP`)
7. Implement optimistic toggle: flip icon + ±1 count → partial DU refresh on stats line → async API call → revert on failure
8. Update thread_view.cpp — tappable ♡ only (⟳ display-only), same optimistic pattern
9. Remove reply-tap → compose logic from thread_view handle (if not already done in Phase 1 step 13)

**Files modified:** atproto.h, atproto_client.h, atproto_client.cpp, feed_view.h, feed_view.cpp, thread_view.h, thread_view.cpp

**Verification:**
1. Feed: tap ♡ → icon fills, count increments, API call succeeds → reload feed → state persists
2. Feed: tap filled ♥ → unfills, count decrements
3. Feed: tap ⟳ → repost toggles similarly
4. Thread: tap ♡ works, ⟳ is not tappable
5. Network failure: optimistic state reverts gracefully

---

## Phase 3.5 — Dual Image Toggles (G3)

*Parallel with Phase 3. Depends on Phase 1.*

**Steps:**
1. Replace `images_enabled` in `app_state_t` with `profile_images_enabled` (default **true**) and `embed_images_enabled` (default **false**)
2. Update settings_view.cpp — two toggle rows: "Profile images" (avatars) and "Embed images" (post images, external embeds)
3. Update `config_load()` / `config_save()` — persist both keys separately
4. Update feed_view.cpp — check `profile_images_enabled` before requesting/drawing avatars; check `embed_images_enabled` before requesting/drawing post images and external embeds
5. Update thread_view.cpp — same conditional checks
6. Update image_cache usage — skip cache lookups entirely when the relevant toggle is off (saves memory and network)

**Files modified:** app.h, settings_view.cpp, config.cpp (or wherever config load/save lives), feed_view.cpp, thread_view.cpp, image_cache.cpp

**Verification:**
1. Default launch: avatars shown, embed images hidden
2. Toggle "Profile images" off → avatars disappear on next refresh
3. Toggle "Embed images" on → post images appear on next refresh
4. Settings persist across restart

---

## Phase 4 — Input Improvements (G5, G6, G7, B12)

*Parallel with Phases 2-3.5. Depends on Phase 0.*

**Steps:**
1. Add `TOUCH_SWIPE_LEFT`, `TOUCH_SWIPE_RIGHT`, `TOUCH_SWIPE_UP`, `TOUCH_SWIPE_DOWN` to `touch_type_t` in input.h
2. Implement swipe detection: track `TOUCH_DOWN` → `TOUCH_UP` displacement + velocity; emit swipe if displacement > threshold and velocity > threshold
3. Add `TOUCH_LONG_PRESS` to `touch_type_t`; emit when `TOUCH_DOWN` held >500ms without movement beyond tap threshold
4. Replace manual scroll tracking in `feed_view_handle()` with `TOUCH_SWIPE_UP`/`TOUCH_SWIPE_DOWN` events (B12 partial — cleaner input contract)
5. Use FBInk's `FBInkState.touch_swap_axes`, `touch_mirror_x`, `touch_mirror_y` flags instead of the `should_swap_axes()` heuristic in input.cpp; keep heuristic as fallback for unknown devices
6. Store FBInk touch flags in `input_state_t` (passed from `fb_t` during `input_open()`)

**Files modified:** input.h, input.cpp, feed_view.cpp (scroll handling), fb.h (expose touch flags)

**Verification:**
1. Swipe up/down in feed → scrolls correctly
2. Long-press on a post → event fires (even if no action is bound yet)
3. Touch coordinates correct on Clara Colour with FBInk-provided transform flags
4. Fallback heuristic still compiles and works for hypothetical unknown device

---

## Phase 5 — Framebuffer & Rendering Improvements (G8, G9, G10, B17, B18)

*Depends on Phase 0. Can overlap with Phase 3+ for partial refresh benefits.*

**Steps:**
1. **G8** — Replace RGB565 color constants in fb.h/fb.cpp with 8-bit grayscale palette: `COLOR_BLACK=0x00`, `COLOR_WHITE=0xFF`, `COLOR_GRAY=0x80`, etc. Remove `rgb565_to_gray()` conversion
2. **G9** — Add `fb_mark_dirty(x, y, w, h)` to track changed regions; add `fb_flush()` that issues a single refresh for the union of dirty rects
3. **G10** — Add `fb_wait_for_complete()` wrapping `fbink_wait_for_complete()` — call after full GC16 refreshes
4. Add `fb_refresh_gc16_partial(x, y, w, h)` for image-region updates with grayscale fidelity
5. **B17** — Replace `fb_refresh_full()` calls in view `_draw()` functions with `fb_mark_dirty()` + let the main loop's `fb_flush()` handle refresh; use partial DU for scroll/stats changes, GC16 partial for image regions
6. **B18** — In `app_run()`, replace `app_switch_view(state, state->current_view)` image-cache redraw path with direct `*_view_draw(state)` call — do NOT reinitialize view state (no `thread_scroll = 0`, no network reload)

**Files modified:** fb.h, fb.cpp, app.cpp, feed_view.cpp, thread_view.cpp, settings_view.cpp, auth_view.cpp

**Verification:**
1. Feed scroll uses partial DU — no full-screen flash
2. Like toggle refreshes only the stats line region
3. Image loading completes without resetting thread scroll position
4. No visual tearing during rapid scroll (completion fencing works)
5. `grep -rn 'rgb565\|RGB565' src/` — zero matches

---

## Phase 5.5 — Remaining Bug Fixes (B4, B5, B7, B8, B9, B11, B15, B16)

*Can be partially parallelized with Phase 5. Some items are independent.*

**Steps (grouped by independence):**

*Independent — can be done in any order:*

1. **B4 — JWT refresh** (atproto_client.cpp, app.cpp):
   - Track token expiry (`last_refresh_time + 90min` or decode JWT `exp`)
   - In `app_ensure_auth()`, check near-expiry → call `mClient->refreshSession()`
   - On any 401/ExpiredToken from API calls, attempt refresh before failing

2. **B5 — Thread reply recursion** (atproto_client.cpp):
   - Add recursive `flattenReplies()` helper per code in bug-gap-report.md
   - Replace single-level loop in `getPostThread()` with recursive descent
   - Add reasonable depth limit (e.g., 10 levels) to prevent stack overflow

3. **B9 — Font buffer overflow** (font.cpp):
   - Add bounds check in `font_draw_wrapped_fallback()`: `if (line_len + word_len + 1 > sizeof(line)) break;`
   - Single-line fix

4. **B7 — Disk cache eviction** (image.cpp or image_cache.cpp):
   - On startup, scan cache dir, sum file sizes
   - Delete oldest files until total < 50MB threshold
   - Alternative: delete files older than 7 days

5. **B8 — In-memory cache LRU** (image_cache.cpp):
   - Add `last_accessed` timestamp to `cache_entry_t`
   - Update on every `image_cache_lookup()` hit
   - In `evict_if_full()`, find and evict entry with oldest `last_accessed`

6. **B16 — Hash collision** (str.cpp, image_cache.cpp):
   - Store full URL string alongside hash in cache entry
   - On lookup, verify URL matches after hash hit
   - Low priority — collision is rare but possible

*Depends on Phase 3 (Post struct has viewer fields):*

7. **B11 — Repost attribution** (atproto_client.cpp, feed_view.cpp):
   - Add `reposted_by` string field to `Bsky::Post`
   - In `getTimeline()`, extract `item->mReason` when it's a repost reason → populate `reposted_by`
   - In feed_view.cpp, render "⟳ reposted by @handle" line above reposted posts

*Deferred / stretch:*

8. **B13 — Suspend/resume**: Not in current scope — would need power button handling, Wi-Fi management, MTK crash workaround. Track as future enhancement.
9. **B15 — Config entry limit**: Bump `CONFIG_MAX_ENTRIES` to 128 (or make dynamic). Can do in Phase 2 when touching config.cpp.

**Files modified:** atproto_client.h, atproto_client.cpp, app.cpp, font.cpp, image.cpp, image_cache.h, image_cache.cpp, str.cpp, feed_view.cpp

**Verification:**
1. B4: Leave app running >2h → feed still refreshes (or simulate by reducing threshold)
2. B5: Open a thread with nested replies → all levels visible
3. B9: Post containing a >512-char URL → no crash
4. B7: After browsing, cache dir stays under 50MB
5. B8: Frequently accessed avatar not evicted while stale images are
6. B11: Feed shows "⟳ reposted by @handle" for reposts

---

## Phase 6 — Cleanup & Documentation

*Last phase. All features and fixes complete.*

**Steps:**
1. Update README.md — remove compose/reply references, document login.txt flow, document `appview` field
2. Remove stale comments referencing removed features throughout codebase
3. Update docs/plugin-port.md if any architecture changes affect the plugin analysis
4. Final `./docker-build.sh` → deploy → full on-device smoke test
5. Update docs/rewrite-plan.md and docs/bug-gap-report.md — mark all items as complete

**Verification:**
1. Docker cross-build succeeds
2. On-device test: full flow from login.txt → feed → thread → like → repost → settings toggles → exit
3. Session resume works after restart
4. `grep -rn 'TODO\|FIXME\|HACK' src/` — review all remaining markers

---

## Relevant Files (complete inventory)

**Always modified:**
- `src/ui/fb.h` / `fb.cpp` — runtime dims, grayscale palette, dirty regions, refresh modes
- `src/ui/font.h` / `font.cpp` — runtime cell size, buffer overflow fix
- `src/ui/input.h` / `input.cpp` — runtime dims, swipe/long-press, FBInk touch flags
- `src/ui/views.h` — enum changes (remove VIEW_COMPOSE/VIEW_LOGIN, add VIEW_AUTH_WAIT)
- `src/ui/feed_view.h` / `feed_view.cpp` — like/repost interaction, swipe scroll, dual toggles
- `src/ui/thread_view.h` / `thread_view.cpp` — like interaction, reply recursion display
- `src/ui/settings_view.h` / `settings_view.cpp` — dual image toggles
- `src/app.h` / `app.cpp` — state struct, login flow, image redraw fix, JWT check
- `src/atproto/atproto.h` — Post struct fields (viewer_like, viewer_repost, reposted_by)
- `src/atproto/atproto_client.h` / `atproto_client.cpp` — like/repost/unlike/unrepost, remove createPost, ViewerState extraction, JWT refresh, thread recursion, repost attribution, appview endpoint
- `src/util/config.h` / `config.cpp` — dual toggles, appview, bump entry limit
- `src/util/paths.h` / `paths.cpp` — login_txt_path
- `src/util/image_cache.h` / `image_cache.cpp` — LRU eviction, disk cache limit
- `src/util/image.cpp` — disk cache eviction
- `src/util/str.cpp` — hash collision fix
- `CMakeLists.txt` — remove compose files, rename login→auth

**Deleted:** compose_view.h, compose_view.cpp, login_view.h, login_view.cpp

**Created:** auth_view.h, auth_view.cpp

---

## Decisions

- **B13 (suspend/resume)**: Deferred — out of scope for v2 MVP. Significant complexity (power button state machine, Wi-Fi toggle, MTK suspend crash workaround).
- **B14 (feed pagination)**: WONTFIX — refresh button + 50-post batch is sufficient.
- **B15 (config limit)**: Folded into Phase 2 as a one-line bump.
- **B16 (hash collision)**: Low priority but included in Phase 5.5 — simple fix.
- **Execution order**: Phase 0 is mandatory first. Phases 1→2 are sequential. Phases 3 and 3.5 can be parallel. Phase 4 is independent. Phases 5 and 5.5 can overlap.

---

## Further Considerations

1. **AppView endpoint wiring**: The ATProto SDK's `Client` class may need its base URL changed for feed/thread queries vs auth. Need to verify whether the SDK supports separate PDS and AppView endpoints, or if we need to swap the host between auth calls (→ PDS) and read calls (→ AppView). *Recommend: check `ATProto::Client` constructor and `setHost()`/`setServiceEndpoint()` during Phase 2 implementation.*
2. **Depth limit for thread recursion** (B5): 10 levels is reasonable but could be configurable. Very deep threads may exhaust stack with recursion — iterative flattening with an explicit stack would be safer. *Recommend: start recursive with depth=10, convert to iterative if needed.*
3. **Image toggle UX**: When toggling images on, should existing posts re-render immediately (triggering a batch of downloads) or only affect new loads? *Recommend: re-render immediately — the user expects to see the change.*
