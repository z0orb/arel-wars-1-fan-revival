# Arel Wars 1 fan revival

A personal project to make **Arel Wars** (Gamevil, Android v1.0.2, 2012) playable again on a modern Android phone, with no server and no network access.

Gamevil's servers are long gone. The original APK also refuses to install on Android 15, and it stops at a network check on every start. This repository holds the **build tooling and patch specs** that turn a copy of the original APK into a working offline build. It does not contain the game, its code, its art or an APK.

## Status

Tested on one phone: a Redmi Note 12 on Android 15 (HyperOS 2).

| Phase | What it proves | Status |
|---|---|---|
| 0 Tooling | One command rebuilds the APK from the original | Done |
| 1 Static audit | Startup, saves, network, shop and threads are understood | Done |
| 2 Device baseline | How the untouched game behaves on Android 15 | Done |
| 3 Offline | The game reaches the title with no network ability. Only `VIBRATE` is requested | Done |
| 4 Modern target | Installs normally at target SDK 28, with no old-app warning | Done |
| 5 Stability | Battles run on a correct 3:2 picture and survive lock, home and relaunch | Done |
| 6 Offline features and saves | Online menus show an "unavailable" message, and saves survive force-stop and updates, with a backup route | Done |
| 7 Release QA | Long soak test and full playthrough | Not started |

## What works

- Story mode, battles and the world map. A stage has been cleared and the progress saved.
- Saves survive force-stop, relaunch and `adb install -r`. A copy of the save is kept in the app's external folder, so it can be backed up without root.
- Sound and music, including after locking the phone.
- PvP, mail, rankings, Export and Import show "This feature is unavailable in the offline version." They never freeze, exit or touch your save.
- Buying Cash or Gold with real money shows "Purchases are unavailable in this offline version." Nothing is charged or credited.

## What doesn't

- **Online play** of any kind. That is by design.
- Only one phone has been tested.

## One balance change

In the original game, Cash came from real-money purchases and PvP. Offline, a normal playthrough earned none, so the six Cash-only heroes and most items were out of reach. **Each stage win now also pays 2 Cash for every Gold it pays.** A full Normal playthrough gives about 87,000 Cash, and Normal plus Hard about 247,000. Everything Cash-only adds up to about 173,000. A loss pays nothing, as before.

## How it is built

`scripts/build` takes the original APK and applies patches in a fixed order:

1. Decodes the code with apktool, and applies `patches/smali/*.patch`. These are not published, because they contain decompiled game code.
2. Edits the binary manifest with `scripts/lib/axml.mjs`, using `patches/manifest/*.json`.
3. Byte-patches the native library with `patches/binary/*.json`. Each patch checks the expected bytes before changing anything. `scripts/lib/gen-textrel.mjs` and `gen-cash.mjs` generate two of them from the original library.
4. Replaces a few strings in the game's text tables with `scripts/lib/zt1text.mjs`.
5. Copies the original APK entry by entry (`scripts/Transplant.java`), replacing only the changed entries and keeping every storage method. This keeps the audio files intact.
6. Aligns and signs it.

`scripts/gates/gate-N` holds the checks each phase had to pass. See [CHANGES.md](CHANGES.md) for every change to the game and why it was made, and [VERIFY.md](VERIFY.md) to check a build yourself.

## Not affiliated

Arel Wars is a trademark of its owner. This project is unofficial and not affiliated with Gamevil or Com2uS. Use your own copy of the game.
