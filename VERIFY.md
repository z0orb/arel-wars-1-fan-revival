# Verifying a build

## Version 2 (`arelwars-v2.0.0.apk`)

| Item | Value |
|---|---|
| File | `arelwars-v2.0.0.apk` |
| SHA-256 | `ea3440fe6ec1303bb44b76effa58b68b344bc875ccb234669ffb590a1dbee57b` |
| Package | `io.github.z0orb.arelwars`, version 2.0.0 (code 200) |
| Minimum and target SDK | 21 and 35 |
| ABIs | `arm64-v8a` and `x86_64` (the emulator bridge), `armeabi-v7a` (the original game library, with no emulator) |
| Permission | `VIBRATE` only. No `INTERNET` |
| Third-party notices | `assets/emu/NOTICE.txt` in the APK, and `emu/docs/NOTICE.md` in this repository |

The v2 key signs the APK. Its certificate:

```
SHA-256 CE:95:AC:F9:50:0F:EC:F5:AE:98:10:9E:43:66:B7:B7:5F:B9:F4:72:0D:06:7C:4C:4A:2A:9D:47:6A:6A:8C:97
```

Do these checks on a PC with the Android SDK build tools:

```
sha256sum arelwars-v2.0.0.apk
apksigner verify --print-certs arelwars-v2.0.0.apk
aapt2 dump badging arelwars-v2.0.0.apk
aapt2 dump permissions arelwars-v2.0.0.apk
```

The `badging` output must show `native-code: 'arm64-v8a' 'armeabi-v7a' 'x86_64'`. The `permissions` output must show only `android.permission.VIBRATE`. `android.permission.INTERNET` must not be in the output.

The v2 APK has the same files as the release build that the test gates used. Only `AndroidManifest.xml` (the package name and the app name) and the signature are different. `emu/tools/apk-diff.mjs` checks this, entry by entry.

### Test results of version 2

Each phase has a gate script in `emu/scripts/gates/`. Each gate passed on the build of the release:

| Gate | Checks |
|---|---|
| `gate-E0` | The emulator runs the game code on the phone. The frame hashes of a test session equal the hashes of the original game code |
| `gate-E1` | The app reaches the title on the phone. The title matches the title of the original game code |
| `gate-E2` | Each Java call of the game works. A stage win gives the same Gold and Cash as v1. Lock cycles in a battle keep the game. The owner checked the sound by ear |
| `gate-E3` | Saves survive force-stop and `adb install -r`. The save import checks each file and keeps the old save. A damaged save gives the same result as in the original |
| `gate-E4` | Each native drawing function gives the same pixels as the original in vector tests. Each test fails when one rule of the port changes |
| `gate-E5` | Target SDK 35. Zero bytes of app traffic with Wi-Fi on. 20 lock and unlock cycles. Online menus show the offline message. A 10,000-event random-input soak with no crash |
| `gate-E6` | The smoke test passes on each device of the compatibility table in [README.md](README.md#compatibility) |
| `gate-E7` | In the largest battle of the test, the phone runs at 20.5 frames each second or more, with the emulator and with no emulator |

The sections below are about v1.

## The original APK

The build only accepts this exact file:

- `00293-Arel-Wars-v1-0-2.apk`, 12.0 MB
- SHA-256 `1b099ef95c359836d080785dd45d741ccb17cc5a40d744854071df6dd2525a17`
- Package `com.gamevil.eruelwars.global`, version 1.0.2 (code 102), signed with a v1 signature, certificate `CN=GameVil`

`scripts/build` stops if the hash differs.

## The patched APK

Builds from this project are signed with this certificate:

```
SHA-256 37:A8:41:5E:76:64:D5:04:EB:CF:64:7C:EA:5C:57:FC:4C:D8:F7:09:CC:12:AB:79:88:80:0D:4E:7D:95:95:06
```

Check it with:

```
apksigner verify --print-certs arelwars-release.apk
```

Android only accepts an update signed with the same key as the installed copy. So an APK with a different certificate can't replace an install from this project. Don't uninstall to get around that, because uninstalling deletes your saves.

## No network access

The only permission the patched APK asks for is `VIBRATE`:

```
aapt2 dump permissions arelwars-release.apk
```

`android.permission.INTERNET` must not appear anywhere in the output, nor in `aapt2 dump xmltree --file AndroidManifest.xml arelwars-release.apk`. Without that permission, Android blocks every socket the app opens.

On the phone:

```
adb shell dumpsys package com.gamevil.eruelwars.global | grep permission
```

## Test results

Each phase has a gate script in `scripts/gates/`. Every gate passed on the real phone (Redmi Note 12, Android 15, HyperOS 2) before the next phase began:

| Gate | Checks |
|---|---|
| `gate-3` | Permissions match the allowlist. Zero bytes of app traffic in `dumpsys netstats` after 3 minutes with Wi-Fi on. A tap on the title changes the screen. Debug and release builds |
| `gate-4` | No `DT_TEXTREL` in the native library. Installs with plain `adb install`. Target SDK 28. No old-app warning |
| `gate-5` | Battle frames advance. Corner controls respond. 20 lock and unlock cycles keep the same process. Back then relaunch works. All 68 audio files keep their storage method. The owner confirmed music and effects by ear |
| `gate-6` | Every online menu shows the offline message within 8 seconds and returns to a usable map. Real-money purchases show their message. Online menus, purchases, Export and Import leave the save unchanged. A stage clear changes the save, and the save survives force-stop, relaunch and `adb install -r`. A backup restores byte-exact and loads |
