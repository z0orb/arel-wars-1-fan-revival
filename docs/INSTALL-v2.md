# Install Arel Wars v2

Version 2 runs on phones with no 32-bit support, and on phones with 32-bit support. On a 64-bit phone, a small ARM emulator in the app runs the original 32-bit game code. On a phone with 32-bit support, you can also use the original game code with no emulator (see "Choose the ABI").

## What you need

- The file `arelwars-v2.0.0.apk` from [the latest release](https://github.com/z0orb/arel-wars-1-fan-revival/releases/latest).
- Android 5.0 (API 21) or later. The tests ran on Android 5.0 to Android 16.
- A phone with an ARM CPU (`arm64-v8a` or `armeabi-v7a`), or an `x86_64` device.

Check the file before you install it. [`VERIFY.md`](../VERIFY.md) gives the SHA-256 and the certificate.

Version 2 has the package name `io.github.z0orb.arelwars` and its own key. It installs next to v1 (`com.gamevil.eruelwars.global`). It does not replace v1, and it does not read the v1 save. To move your save, follow "Move your save from v1".

## Install route

This project has no developer registration with Google. So the install route depends on your country.

### Brazil, Indonesia, Singapore and Thailand

From September 2026, Android in these countries blocks a normal install of an app from an unverified developer. You have two routes:
1. **The advanced flow.** Android lets you allow apps from unverified developers. The procedure needs the developer options, a restart of the phone and a wait of 24 hours. You do it one time. After that, you can install the APK from a file manager. Google describes the procedure in [Understanding Android developer verification](https://support.google.com/android-developer-console/answer/16561738).
2. **adb from a PC.** Developer verification does not apply to adb installs. See "Install with adb".

### All other countries

Install the APK from a file manager:
1. Copy the APK to the phone.
2. Open the APK in a file manager.
3. If Android asks, allow "Install unknown apps" for the file manager.
4. Tap "Install".

## Play Protect

Play Protect can stop the install with the message "Unsafe app blocked". The result can change from one install to the next, because it comes from a server of Google. This message does not mean that the app has a fault. The app has no network permission, and it cannot send data.

If you see this message:
1. Do not tap the default button. The default button cancels the install.
2. Tap "More details".
3. Tap "Install anyway".

Play Protect does not scan adb installs when the setting `verifier_verify_adb_installs` is 0.

## Install with adb

1. On the phone, turn on the developer options and "USB debugging".
2. Connect the phone to the PC with a USB cable.
3. Accept the debug prompt on the phone.
4. On the PC, run this command:

```
adb install -r arelwars-v2.0.0.apk
```

To update, run the same command with the new APK. **Do not uninstall first.** An uninstall deletes the save.

## Xiaomi, Redmi and POCO phones (HyperOS)

The tests ran on a Redmi Note 12 with HyperOS 2 (Android 15). These items are normal on HyperOS:
- **Install prompt.** HyperOS can ask you to accept each install, also each update with `adb install -r`. Accept it on the phone. If nobody accepts it, adb shows `INSTALL_FAILED_USER_RESTRICTED`. Then run the command again and accept the prompt.
- **Install check.** HyperOS can close its install check by itself, and adb then shows "canceled by user". Run the command again.
- **Install from a file manager.** HyperOS asks you to allow installs for the file manager. Then it shows an install confirmation with a wait of 10 seconds. Tap the button when the wait ends.
- **Security scan.** After an install, HyperOS can show its own security scan. Let it complete. Then close the result page ("Passed security tests") with the back button before you open "Arel Wars Save Import". If that page stays open, it can come to the front during the import.
- **Test result (2026-11-18).** On the test phone, the install from a file manager gave no Play Protect message. The security scan said "Passed security tests" and "No risks detected".
- **Frame boost.** On the `armeabi-v7a` path (see "Choose the ABI"), logcat shows lines with the tag `FPS-BOOST` and the text `libboost.so" not found`. These lines come from HyperOS, not from the game. The game runs at its normal speed.
- **Picture shape.** The game shows at its own 3:2 shape, with black bars at the sides. The system bars are hidden. Swipe from the edge to show them for a short time.

## Choose the ABI

A normal install on a 64-bit phone uses the emulator (`arm64-v8a`). The emulator runs the game at its full speed (about 21 frames each second, the speed of the original).

A phone that has 32-bit support can also run the original game code with no emulator:

```
adb install -r --abi armeabi-v7a arelwars-v2.0.0.apk
```

The save stays the same for both ABIs. A phone with no 32-bit support (for example Pixel 7 and later) can use only the emulator.

## Move your save from v1

The v1 game copies its save to this folder:

```
/sdcard/Android/data/com.gamevil.eruelwars.global/files/saves/
```

From Android 11, the system folder picker cannot open folders in `Android/data`. So copy the folder to a normal folder first.

1. Copy the whole `saves` folder to a normal folder, for example `Download/ArelWars-saves`. Keep the files together (`Save0.dat`, `Sng0.dat`, `option.sav` and the other slots). With a PC, use these commands:

```
adb pull /sdcard/Android/data/com.gamevil.eruelwars.global/files/saves/ ArelWars-saves
adb push ArelWars-saves /sdcard/Download/
```

   Without a PC, use a file manager that can open `Android/data`.
2. Open the app "Arel Wars Save Import". The install adds it to the app list, next to "Arel Wars".
3. In the folder picker, open the folder that you made in step 1. Tap "Use this folder", then "Allow".
4. The import checks each file. If a file is damaged, the import shows an error and changes nothing.
5. Tap "OK". Then open "Arel Wars" and tap "Continue".

The import keeps the old save of v2 in the app folder, so an import does not delete a save.

**Keep v1 until you know that your save works in v2.** Uninstall v1 only after you play your save in v2.

## Back up the v2 save

The v2 game copies its save to this folder:

```
/sdcard/Android/data/io.github.z0orb.arelwars/files/saves/
```

Copy that folder to a safe place from time to time. To put a backup back, use "Arel Wars Save Import" with a copy of the folder.

> [!WARNING]
> An uninstall, or "Clear data" in the Android settings, deletes the save. To update, always install over the old version.
