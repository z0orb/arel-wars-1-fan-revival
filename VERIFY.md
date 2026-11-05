# Verifying a build

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
