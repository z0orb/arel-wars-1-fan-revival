# Target 35: the rules that change for the v1 Java

TASK-E5.1 of `PLAN.md`. The emu manifest patch `shell/patches/emu/manifest/0003-target35.json` sets `minSdkVersion` 21 and `targetSdkVersion` 35. This file records each target-gated rule, and the result for the v1 Java after the v1 patches.

Method: a search of the patched smali tree (`work/shell-debug/decoded/smali`) for each API, then a check of the method that holds each match. "Cut by v1" means that the v1 stubs (`0001-sever.patch`) or the v1 manifest patch (`0001-strip.json`) delete the only path to that code. The v1 gates 3, 5 and 6 tested those cuts at target 28.

| Rule (target) | What the v1 Java does | Result |
|---|---|---|
| `android:exported` on each component with an intent filter (31) | Three activities have intent filters: `DRMLicensing` (launcher), `SkeletonLauncher` (action `MAIN`, category `DEFAULT`), `ArelImport` (launcher). The v1 strip deleted all receivers and services | `DRMLicensing` and `ArelImport` get `exported` true. `SkeletonLauncher` gets `exported` false. `DRMLicensing` starts it with an explicit intent (`Intent(Context, Class)`), so the start works |
| Implicit intents to components that are not exported (34) | The only start of `SkeletonLauncher` uses an explicit intent | No change necessary |
| `registerReceiver` needs `RECEIVER_EXPORTED` or `RECEIVER_NOT_EXPORTED` (34) | No call of `registerReceiver` in the smali tree | No change necessary |
| `PendingIntent` needs `FLAG_IMMUTABLE` or `FLAG_MUTABLE` (31) | No call that makes a `PendingIntent`. `PurchaseObserver.startBuyPageActivity` only gets one, and the billing path is cut by v1 | No change necessary |
| `POST_NOTIFICATIONS` (33) | No `NotificationManager` call in the smali tree. The local alarms are v1 stubs | No change necessary |
| Apache HTTP is not on the boot class path (28) | `org/apache/http` is only in Google Analytics, Tapjoy and Flurry code. All three are cut by v1 | No change necessary. Same as v1 at target 28 |
| `bindService` with an implicit intent throws (21) | Only `BillingService.bindToMarketBillingService`, cut by v1 | No change necessary |
| `MODE_WORLD_READABLE` and `MODE_WORLD_WRITEABLE` throw (24) | Each `getSharedPreferences` and `openFileOutput` call with a constant mode uses mode 0 (`MODE_PRIVATE`) | No change necessary |
| `getDeviceId` and other device identifiers need a privileged permission (29) | No call of `getDeviceId`, `getLine1Number`, `getSubscriberId` or `Build.getSerial`. `NexusUtils.getDeviceID` and `getMacAddress` return the frozen v1 values | No change necessary |
| Package visibility (30) | `getRunningAppProcesses` only in `NexusTorchwood.checkTasks`, cut by v1 (`doTorchwood` is a stub) | No change necessary |
| Rotation and size changes recreate the activity, unless `configChanges` names them (13 and later) | v1 sets `0xda0` (keyboard hidden, orientation, screen layout, screen size, smallest screen size) | Now `0x1da0` on each activity: `density` is added (DES-11) |
| Edge-to-edge is forced (35) | v1 patch `0003-aspect` centres a 3:2 box in the window, and does not know the system bars or the cutout | New class `ArelInsets` (TASK-E5.2) |
| Predictive back (33, with `enableOnBackInvokedCallback`) | `SkeletonLauncher.onKeyDown` sends the game key -8 for `KEYCODE_BACK` | The manifest sets `enableOnBackInvokedCallback` true. New class `ArelBack` (TASK-E5.2) gives the activity the back key from the callback |
| `resources.arsc` must be stored and 4-byte aligned (30) | The original APK stores it. `zipalign` aligns it | No change necessary |
| Native libraries: 16 KB page alignment | `zipalign -P 16` (DES-15) | No change necessary |
