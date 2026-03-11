# Nickel Plugin Port

## Current State

sKeets is currently a standalone framebuffer application built with Qt6 Core and Qt6 Network. It launches outside Nickel, takes over the framebuffer, and stores persistent state under `/mnt/onboard/.adds/sKeets`.

The open Kobo plugin examples in this workspace target Nickel's QWidget plugin interface and the Qt Embedded 4.6 toolchain. That is a different ABI and toolchain from the current sKeets build.

## What Is Guaranteed If sKeets Moves Into Nickel

1. Network access can continue to use Qt's network stack.
   sKeets already performs HTTP requests through Qt network classes and the ATProto SDK. A Nickel plugin does not need raw socket privileges; it relies on the same userland network stack Nickel uses.

2. Config and cache files can continue to live under `/mnt/onboard/.adds/sKeets`.
   Nickel plugins can write to user storage. This repo now resolves the data root at runtime and defaults to `/mnt/onboard/.adds/sKeets`, with `SKEETS_DATA_DIR` as an override for testing.

3. Native touch dispatch and keyboard handling become feasible only inside Nickel.
   A QWidget-based plugin can participate in focus and input handling. The current standalone framebuffer architecture cannot use Nickel's input method stack because it kills Nickel before launch.

## Hard Blockers To A Direct Port

1. Qt version mismatch.
   The open Nickel plugin examples use Qt Embedded 4.6, while sKeets currently depends on Qt6.

2. Toolchain mismatch.
   The public Kobo build docs describe plugin builds via the old Qt Embedded qmake flow, not the current CMake plus Qt6 flow used by sKeets.

3. Backend compatibility risk.
   The current ATProto dependency stack and C++23 usage are unlikely to build unchanged against the old Nickel plugin environment.

## Recommended Migration Plan

1. Keep the backend portable.
   Avoid hardcoded storage paths and isolate networking, config, image cache, and ATProto state management from framebuffer-specific code.

2. Replace the standalone UI shell.
   Rebuild the UI as a QWidget hierarchy hosted by Nickel instead of rendering directly through FBInk.

3. Replace or downgrade incompatible dependencies.
   Expect to replace the Qt6-based ATProto integration with a backend that can build against the plugin toolchain.

4. Preserve the on-device storage layout.
   Continue using `/mnt/onboard/.adds/sKeets/config.ini` and `/mnt/onboard/.adds/sKeets/cache` so existing data survives the migration.

## Practical Conclusion

This repository can be made plugin-ready in stages, but it cannot honestly be converted into a working Nickel plugin without replacing the Qt6-based application shell and likely parts of the network/backend stack.