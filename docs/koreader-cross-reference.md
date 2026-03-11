# KOReader Cross-Reference: Bugs & Feature Gaps for sKeets

## 1. Touch Axis Transforms Per Device Model

### How KOReader Does It

KOReader's Kobo base class ([device.lua](../../../koreader/frontend/device/kobo/device.lua)) defines three boolean flags per device:

```lua
touch_switch_xy = true     -- almost ALL Kobos swap X/Y
touch_mirrored_x = true    -- most Kobos mirror X
touch_mirrored_y = false   -- a few newer devices mirror Y instead
```

The actual transform is built as a **composite hook** in `Kobo:initEventAdjustHooks()` (line ~1120). There are three code paths:

| Condition | Transform Function | Devices |
|---|---|---|
| `switch_xy + mirrored_x` | `adjustABS_SwitchAxesAndMirrorX(ev, max_x)` | Most Kobos (Clara HD, Forma, Libra, etc.) |
| `switch_xy + mirrored_y` | `adjustABS_SwitchAxesAndMirrorY(ev, max_y)` | Elipsa 2E (condor), Libra Colour (monza), Clara Colour |
| `switch_xy` only | `adjustABS_SwitchXY(ev)` | H2O2 (snow), Libra 2 (io) |

The transform functions (in `frontend/device/input.lua` ~L456-514) operate at the raw `EV_ABS` level:
- Swap `ABS_X ↔ ABS_Y` and `ABS_MT_POSITION_X ↔ ABS_MT_POSITION_Y`
- Then mirror by subtracting from `max_x` or `max_y` as needed

**`max_x`** = `screen:getWidth() - 1`, **`max_y`** = `screen:getHeight() - 1`.

### sKeets Gap

sKeets (`src/ui/input.cpp`) does axis normalization but likely assumes a single transform. Newer MTK devices (Elipsa 2E, Libra Colour, Clara Colour) use `touch_mirrored_y = true` instead of `touch_mirrored_x`, which would produce inverted touch on those models if sKeets always mirrors X.

**Action needed:** Add per-device model detection in `device.cpp` and select the correct transform (XY+MirrorX vs XY+MirrorY vs XY-only).

---

## 2. EPDC Waveform Mode Selection

### How KOReader Does It

KOReader has **three entirely different driver backends** for Kobo, selected at init time:

| SoC Family | Driver Backend | Waveform Modes | Kobo Devices |
|---|---|---|---|
| **NXP i.MX6** (pre-Mk7) | `refresh_kobo` via `MXCFB_SEND_UPDATE_V1_NTX` | INIT, DU, GC16, A2, GL16, REAGLD, AUTO | Aura, Glo, H2O, Touch |
| **NXP i.MX6** (Mk7) | `refresh_kobo_mk7` via `MXCFB_SEND_UPDATE_V2` | Same + REAGL, GCK16, GLKW16 + HW dithering | Clara HD, Forma, Libra, Nia |
| **MediaTek** (MTK) | `refresh_kobo_mtk` via `HWTCON_SEND_UPDATE` | HWTCON_WAVEFORM_MODE_* + CFA processing + swipe animations | Elipsa 2E, Libra Colour, Clara B/W, Clara Colour |

The `framebuffer_mxcfb.lua` init (~line 960) sets up waveform mode constants per-device:

```
waveform_partial  → REAGL (Mk7+) / AUTO (pre-Mk7) / GLR16 (MTK)
waveform_fast     → DU (all)
waveform_ui       → AUTO (all Kobo) 
waveform_full     → GC16 (all)
waveform_a2       → A2 / HWTCON_A2
waveform_night    → GLKW16 (eclipse-capable) / GC16 (older)
waveform_flashnight → GCK16 (eclipse-capable) / GC16 (older)
```

Key implementation details:
- **Marker tracking**: Every refresh gets an incrementing marker. Full updates block on `WAIT_FOR_UPDATE_COMPLETE`.
- **REAGL handling**: Mk7 doesn't need PARTIAL→FULL promotion (EPDC fences internally). MTK *does* need it (except Elipsa 2E).
- **HW dithering**: Mk7 uses `EPDC_FLAG_USE_DITHERING_ORDERED` with quant_bit=1 (for A2/DU) or 7 (others). MTK uses `HWTCON_FLAG_USE_DITHERING` + Y8→Y4/Y1 sub-modes.
- **Night mode**: On NXP, uses `EPDC_FLAG_ENABLE_INVERSION` per-request. On MTK, sets a global `/proc/hwtcon/cmd` flag (`night_mode 4`/`night_mode 0`).
- **Kaleido (color)**: MTK color devices get `GCC16` (for full) and `GLRC16` (for REAGL) waveform modes, plus `HWTCON_FLAG_CFA_EINK_G2` post-processing.
- **Unreliable WAIT_FOR_COMPLETE**: Libra, Libra 2, Clara 2E, Nia have `hasReliableMxcWaitFor = false` — KOReader falls back to a 2.5ms sleep stub or does paired waits.
- **EPDC power management**: On Mk9+ NXP, writes `1,0` to `/sys/class/graphics/fb0/power_state` before every refresh. On MTK, writes `fiti_power 1` to `/proc/hwtcon/cmd`.
- **Alignment constraints**: MTK requires 16-pixel alignment for refresh regions (vs 8 for dithering on NXP).

### sKeets Gap

sKeets uses FBInk for framebuffer access, which handles many of these details internally. However:
- sKeets hardcodes `GC16` for full refresh and `DU` for fast — it doesn't use REAGL for partial page updates, which means more ghosting on content reflows.
- No EPDC wake-up call before refreshes (the `mech_poweron` pattern). This could cause refresh failures on newer boards.
- No marker-based wait-for-completion — could cause visual artifacts on fast sequential updates.
- No MTK/HWTCON awareness at all — newer devices won't work properly.

---

## 3. Power Button / Sleep / USB Events

### How KOReader Does It

Power management is orchestrated across `device/generic/device.lua:onPowerEvent()` and `device/kobo/device.lua`:

**Power button flow:**
1. `PowerPress` → schedules `poweroff_action` in 2 seconds (long-press = shutdown)
2. `PowerRelease` → cancels poweroff; if awake → `Suspend`; if in screensaver → `Resume` (unless sleep cover locked)

**Suspend sequence** (Kobo-specific, ~L1330):
1. Show screensaver widget, force repaint
2. Kill Wi-Fi (critical — suspend with Wi-Fi on crashes the kernel!)
3. Turn off frontlight
4. Write `1` to `/sys/power/state-extended` (flags kernel subsystems for suspend)
5. Wait 2 seconds (letting the kernel settle)
6. `sync` filesystem
7. Write `mem` to `/sys/power/state` (actual suspend-to-RAM)
8. On wakeup: schedule `checkUnexpectedWakeup` guard (15s timer)

**Standby (lighter sleep):**
- Available on Mk7 and Sunxi (NOT safe on MTK — hangs kernel!)
- MTK also crashes on suspend while charging — KOReader explicitly skips suspend/standby if `isMTK() and isCharging()`
- Writes device's `standby_state` to `/sys/power/state` (usually `"standby"`, but `"mem"` on MTK)

**Resume:**
1. Reset `unexpected_wakeup_count`
2. Write `0` to `/sys/power/state-extended`  
3. Sleep 100ms for kernel catch-up
4. IR grid touch wake-up command if applicable
5. Restore charging LED

**Unexpected Wakeup Guard:**
- After resume from suspend, a 15s timer checks if this was a scheduled alarm wakeup or unexpected
- If unexpected: re-suspends. After 20 consecutive unexpected wakeups → broadcasts `UnexpectedWakeupLimit` event and gives up

**USB events:**
- `UsbPlugIn` → if awake, potentially start USB Mass Storage mode; if suspended, just re-suspend
- `UsbPlugOut` → dismiss USBMS; re-suspend if in screensaver mode
- `Charging`/`NotCharging` → same pattern but for power-only chargers

**Sleep cover:**
- `SleepCoverClosed` → `Suspend()`
- `SleepCoverOpened` → `Resume()`
- Can be configured to ignore one or both events

### sKeets Gap

sKeets `run.sh` handles power at the shell level only:
- Kills Nickel, starts watchdog feeder
- The watchdog feeder writes `.` to `/dev/watchdog` every 10s — if sKeets crashes, device hard-reboots in ~60s
- No suspend/resume handling in the C++ code at all
- No USB event handling
- No sleep cover support
- Power button events are read via input but there's no sleep/wake state machine

**Critical bugs:**
- If the device suspends (e.g., via battery timeout), sKeets won't handle it gracefully
- MTK devices would crash if suspend is attempted while charging
- No Wi-Fi teardown before sleep
- No unexpected wakeup guard

---

## 4. Watchdog Handling

### KOReader
KOReader's startup script (`platform/kobo/koreader.sh`) also feeds the watchdog, similar to sKeets. The approach in the community is the same — once Nickel is killed, userspace must feed `/dev/watchdog` to prevent a hardware reset.

KOReader doesn't do anything special with the watchdog in the Lua code — it's entirely in the shell wrapper.

### sKeets
sKeets `run.sh` does this correctly:
```bash
exec 3>/dev/watchdog || exit 0
while true; do printf '.' >&3 || exit 0; sleep 10; done
```
This is solid. The `|| exit 0` on the write means if the fd is lost (process dying), the subshell exits cleanly. The 10s interval is fine (hardware watchdog timeout is ~60s). No gap here.

---

## 5. Session/Token Refresh Patterns

### KOReader
KOReader doesn't use ATProto. Its network interactions are:
- HTTP downloads (OPDS, OTA updates) with basic/digest auth
- Wi-Fi management via shell scripts
- No persistent auth sessions to refresh

### sKeets
sKeets stores `access_jwt`, `refresh_jwt`, and `did` in `config.ini`. On startup:
1. If saved session exists → `resumeSession()` with saved tokens
2. If pre-filled credentials → `createSession()` then scrub password
3. If resume fails → falls through to login view

**Gap:** The code only attempts `resumeSession` once at startup. There's no token refresh during a running session — if the access JWT expires mid-use (ATProto access tokens last ~2 hours), API calls will start failing. The `refresh_jwt` is stored but may never be used after initial resume.

**Action needed:** Implement a retry-with-refresh pattern: on 401/expired, call `refreshSession()` with the stored refresh JWT, update stored tokens, then retry the failed request.

---

## 6. Image Caching Patterns

### KOReader
KOReader's cache system (`frontend/cache.lua`) is a generic LRU cache built on `ffi/lru.lua`:
- Configurable by **slot count** (e.g., CatalogCache = 20 slots) or **byte budget** (e.g., DocCache with eviction callbacks)
- LRU eviction based on access order
- Optional **disk cache** — DocCache stores rendered pages to `cache_path` with MD5-hashed keys
- **CacheItem** objects with `onFree()` callbacks for cleanup (freeing C memory, etc.)
- Separate specialized caches: GlyphCache, TileCacheItem, DocCache, CatalogCache

For OPDS thumbnails specifically, the OPDS browser doesn't cache images — it downloads and shows them inline.

### sKeets
sKeets (`src/util/image_cache.h/cpp`) has:
- Async download via Qt NetworkAccessManager
- Disk cache with `SKIC` magic header files under `/mnt/onboard/.adds/sKeets/cache/`
- Memory cache (presumably an unordered_map or similar)
- `image_cache_redraw_needed()` flag for deferred rendering

**Gap:** No LRU eviction — the disk cache grows unbounded. No size limit on the memory cache either. Over time, the cache directory will consume significant storage on the Kobo's limited internal storage.

**Action needed:** Add LRU eviction to both memory and disk caches. Consider a max disk cache size (e.g., 50MB) and max memory cache size.

---

## 7. Pagination Patterns for Feed-Like Views

### KOReader OPDS Browser
The OPDS browser (`plugins/opds.koplugin/opdsbrowser.lua`) extends `Menu` (a paginated list widget) and implements feed pagination:

**Pagination mechanism:**
1. Parse ATOM XML catalog; extract `<link rel="next">` URL from feed links:
   ```lua
   if link.rel and link.href then
       hrefs[link.rel] = build_href(link.href)
   end
   ```
2. Store `hrefs.next` in the item table
3. On "next page" chevron tap (`onNextPage`):
   ```lua
   while page_num == self.page_num do
       if hrefs and hrefs.next then
           self:appendCatalog(hrefs.next)  -- fetch + append items
       else break end
   end
   Menu.onNextPage(self)  -- then paginate locally
   ```
4. `appendCatalog()` fetches the next URL, parses entries, appends to current item table, and updates `hrefs` to the new page's links

**Key design:**
- Items accumulate in memory — the full browsed catalog is kept
- Local pagination (Menu widget handles pages of ~items-per-screen) is independent of network pagination
- CatalogCache (20-slot LRU) caches parsed XML to avoid re-fetching on back navigation
- Next-page fetch happens **on demand** when the user taps the next-page chevron, not on scroll

### sKeets
sKeets `feed_view` uses ATProto's `cursor`-based pagination:
- `getTimeline(cursor)` returns a page of posts + a cursor string for the next page
- Feed is rendered as a vertically-scrollable list with `compute_layout()` + clip-to-viewport
- Scrolling moves a virtual scroll offset

**Potential gap:** 
- If sKeets only loads one page and doesn't fetch more on scroll, users see a limited timeline
- No "load more" trigger when reaching the bottom of the current page
- No caching of previously-loaded pages when navigating away and back

**Action needed:** Implement a scroll-triggered "load more" pattern: when scroll position reaches near the bottom of current content, fetch next page via cursor and append to the post list. Store cursor in feed state. Consider keeping N pages in memory and discarding oldest when memory pressure is high.

---

## Summary of Critical Findings

| Area | Severity | Issue |
|---|---|---|
| Touch transforms | **High** | MTK devices (Elipsa 2E, Libra Colour, Clara Colour) use `mirrored_y` instead of `mirrored_x` — touch will be wrong |
| EPDC waveforms | **Medium** | No REAGL for partial updates (more ghosting); no MTK HWTCON support; no EPDC wake-up |
| Suspend/Resume | **High** | No power management — device may crash on MTK if it enters suspend while charging; no Wi-Fi teardown |
| Token refresh | **High** | Access JWT expires after ~2h with no refresh mechanism — app becomes unusable until restart |
| Image cache eviction | **Medium** | Unbounded disk cache will eventually fill storage |
| Feed pagination | **Medium** | Limited to single page load; no infinite scroll |
| Watchdog | **None** | Correctly handled in run.sh |
