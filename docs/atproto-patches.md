# ATProto Patches And Workarounds

This project uses the `mfnboer/atproto` C++ SDK, pinned to a specific commit and patched locally for Kobo cross-compilation.

## Upstream Dependency Pin

- Source: `https://github.com/mfnboer/atproto.git`
- Pinned commit: `2a1019bbe1e15a32b8ae678378c7adaf7e5dbf75`
- Build wiring: `CMakeLists.txt` via `FetchContent_Declare(atproto_sdk, ...)`

The SDK is pinned because there is no stable release tag in use here, and sKeets depends on the API shape of that snapshot.

## Build-Time Patch Script

The SDK is patched during fetch/build by `cmake/patch-atproto.cmake`.

These are real local patches to upstream sources:

1. Disable ASAN and related sanitizer flags from the SDK top-level `CMakeLists.txt`.
2. Remove coverage-oriented flags and `-lgcov` that break Kobo cross-compilation.
3. Replace Qt Quick / QuickControls2 linkage with the smaller Core + Network + Qml set used by sKeets.
4. Insert a missing `VERSION 1.0` into `qt_add_qml_module(...)`.
5. Replace `QString::slice()` with `chop()` because the target environment is on Qt 6.5, not Qt 6.8+.
6. Fix code that assumes `QObject::moveToThread()` returns `bool`; on older Qt it returns `void`.
7. Adjust a stale `Xrpc::Client` constructor call in `test.h`.

These patches exist to make the pinned SDK build cleanly in the Kobo toolchain, not to change protocol behavior.

## Runtime And Wrapper Workarounds

These are not upstream patches, but they are project-local workarounds around SDK behavior or API shape.

### Fresh AppView client instead of `changeHost()`

The rewrite uses a fresh `Bsky::AtprotoClient(appview)` for feed and thread reads instead of resuming a PDS client and then switching it to AppView with `AtprotoClient::changeHost()`.

Reason: switching a resumed client over to AppView triggered heap corruption on-device (`malloc(): unsorted double linked list corrupted`).

Current pattern:

- `src/rewrite/feed.cpp`: construct a new client with `appview`, then call `resumeSession(session, ...)`
- `src/rewrite/thread.cpp`: same pattern for thread fetches

The older host-switching helper still exists in `src/atproto/atproto_client.cpp`, but the rewrite path avoids it for AppView reads.

### Reply tree flattening

The SDK returns thread replies as a nested tree. sKeets flattens that tree recursively in its wrapper so the UI can render a linear reply list.

Implementation: `flattenReplies(...)` in `src/atproto/atproto_client.cpp`.

This is an application-side adaptation, not an SDK patch.

### `PostMaster` call shape

In the pinned SDK snapshot, `PostMaster::like()` and `PostMaster::repost()` require `viaUri` and `viaCid` parameters even when unused.

sKeets passes empty `QString()` values for those arguments in its wrapper methods:

- `AtprotoClient::likePost(...)`
- `AtprotoClient::repostPost(...)`

This is a usage workaround for the pinned API, not a code patch.

### Viewer state extraction

Current `convertPostView()` code extracts `mViewer->mLike` and `mViewer->mRepost` into sKeets fields so the app can track the user's like/repost state.

Note: some older analysis docs still say ViewerState was not extracted. That is stale; the current wrapper does extract it.

## Summary

There are two categories of ATProto-related local changes in sKeets:

1. Real upstream patching in `cmake/patch-atproto.cmake` to make the pinned SDK build under the Kobo Qt 6.5 cross-toolchain.
2. App-side wrapper/runtime workarounds in `src/atproto/` and `src/rewrite/` to avoid unsafe host switching and to adapt the SDK's response/API shapes to the UI.