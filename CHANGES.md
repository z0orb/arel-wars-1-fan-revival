# Changes to the game

Every change made to the original APK, in plain language, with the reason. Each one can be reverted on its own.

## Network and privacy

- **The startup network check is removed.** The original game checked a Gamevil server every time it came to the front, and closed or showed "Network Error" when it couldn't reach it.
- **The game reports that no network is available.** Every in-game online feature then fails cleanly into its own error message, instead of trying to open a socket.
- **Analytics, ad, push-message and licence-server code is never started** (Flurry, Google Analytics, Tapjoy, C2DM). The calls keep their names and signatures, because the native code looks them up by name.
- **Device identifiers are replaced with fixed fake values.** The game no longer reads the phone number, IMEI, MAC address, SIM serial or Android ID. The save file is tied to a device ID, so the fake values never change.
- **The game no longer sends the phone number and model** to a Gamevil server over a raw socket.
- **Every permission except `VIBRATE` is removed**, including `INTERNET`. The game cannot reach the network even if something tried. Billing, Tapjoy and C2DM components and a `gamevil://` link handler are removed from the manifest too.

## Running on Android 15

- **Target SDK raised to 28.** Android 15 refuses to install apps that target an SDK below 24.
- **Text relocations removed from the native library.** Android refuses them from target 23. The 12 affected entries are only read by an exception unwinder the game never runs, so they are marked as no-ops.
- **A wrong JNI call is fixed** in `isEnglish`. The game called a boolean Java method as if it returned an object, which modern Android's checks abort on.
- **Three pointer checks are fixed** in the native code. They treated memory addresses above 2 GB as invalid, which is normal on a modern phone, so every battle crashed while loading the map.
- **Timers run in parallel again.** On newer Android, background tasks queue one at a time by default, which delayed the game's timers.
- **The game is shown at its own 3:2 shape** with black bars at the sides, instead of being stretched 24% wider. Taps follow the picture.
- **Both screens are landscape**, which removes a portrait flash at startup.
- **Exit closes the process cleanly.** The original used a permission that is gone, and leaving the old process alive crashed on the next start.
- **Music restarts after the phone is unlocked.**

## Offline features

- **Online menus show "This feature is unavailable in the offline version."** This replaces the connect failure text, which showed an unrelated error number. In Korean too.
- **Export and Import say they are unavailable and that your game data will not change.** The original text warned that your data would be deleted, which cannot happen offline.
- **The Data Management text says online backup is unavailable** and that your data stays on the phone.
- **Real-money Cash and Gold purchases show "Purchases are unavailable in this offline version."** Nothing is reported back to the game, so nothing can be credited by mistake.
- **Stage wins pay Cash: 2 Cash per Gold earned.** Offline, Cash could only come from Hard-mode replays, a few at a time, so Cash-only heroes and items were unreachable. The result screen's Gold call now goes through a small helper that also adds the Cash, using the game's own checked setter, so the save's Cash checksum stays valid. The helper lives in a trial-version function the game never calls. Spending Cash works as before.
- **A copy of the save is kept up to date** in `Android/data/com.gamevil.eruelwars.global/files/saves`, which can be copied off the phone over USB without root. It never replaces a save with an empty one.
