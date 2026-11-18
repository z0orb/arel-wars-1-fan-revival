# Changelog

## v2.0.0 — 2026-11-18

The emulator build. See [CHANGES.md](CHANGES.md), section "Version 2", for the full list. See [docs/INSTALL-v2.md](docs/INSTALL-v2.md) for the install and the save import.

- Runs on phones with no 32-bit support. On `arm64-v8a` and `x86_64`, an embedded ARM emulator runs the original game code. On `armeabi-v7a`, the original game code runs with no emulator.
- New package `io.github.z0orb.arelwars` with a new key. It installs next to v1.
- New app "Arel Wars Save Import". It imports a v1 save set, and it checks each file first.
- Target SDK 35, minimum SDK 21 (Android 5.0). `VIBRATE` is still the only permission.
- The game uses the full 3:2 box of the screen, clear of the cutout, with the system bars hidden.
- Sound effects play on a worker thread. The game keeps its full speed in the largest battles.

Tests: all automated gates pass on the release build. The tests ran on a Redmi Note 12 (Android 15, HyperOS 2) and on Android emulators from API 21 to API 36. The emulators had phone, small, tablet and foldable screens. [README.md](README.md) has the compatibility table.

## v1.0.0 — 2026-11-06

First release: the first offline build. See [CHANGES.md](CHANGES.md) for the full list.

- Plays offline on Android 15, at target SDK 28, with `VIBRATE` as the only permission.
- Battles, story, world map, shop, saves and audio work.
- Online menus and real-money purchases show an "unavailable offline" message.
- Saves survive force-stop, relaunch and updates, and are mirrored to the app's external folder for backup.
- Stage wins pay 2 Cash per Gold earned, so Cash-only heroes and items can be bought offline.

Tested on a Redmi Note 12, Android 15: every automated gate passes on the release build, including a 50,000-event random-input soak with no crash in the game. Stages 1 and 2 were played by the test scripts. Later stages, Hard mode and all heroes have not been played through yet. Please report anything that breaks.
