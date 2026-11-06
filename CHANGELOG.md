# Changelog

## v1.0.0 — 2026-11-06

First release: the first offline build. See [CHANGES.md](CHANGES.md) for the full list.

- Plays offline on Android 15, at target SDK 28, with `VIBRATE` as the only permission.
- Battles, story, world map, shop, saves and audio work.
- Online menus and real-money purchases show an "unavailable offline" message.
- Saves survive force-stop, relaunch and updates, and are mirrored to the app's external folder for backup.
- Stage wins pay 2 Cash per Gold earned, so Cash-only heroes and items can be bought offline.

Tested on a Redmi Note 12, Android 15: every automated gate passes on the release build, including a 50,000-event random-input soak with no crash in the game. Stages 1 and 2 were played by the test scripts. Later stages, Hard mode and all heroes have not been played through yet. Please report anything that breaks.
