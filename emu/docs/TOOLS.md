# Emulator path tools

This file records each download of the emulator path (`PLAN.md` 8.6). The v1 tools are in `../../docs/TOOLS.md`. The Phase 8 tools are in `../../universal/docs/TOOLS.md`. The emulator path uses both sets in read-only mode.

## Network on 2026-11-09

The PC was on the hotspot of the phone (SSID `ganti router pls, down terus`, interface `wlan1` tethered to `rmnet_data3`). So the downloads used mobile data. All downloads together are 5.9 MB plus the CMake package. No single download is more than 100 MB.

## Downloads

| Name | Version or commit | Source | Size (bytes) | SHA-256 | Date |
|---|---|---|---|---|---|
| dynarmic | `a46601580d5512d324104f985b5f0209dc980ddc` (fork `azahar-emu/dynarmic`, project version 6.7.0) | `https://codeload.github.com/azahar-emu/dynarmic/tar.gz/<commit>` | 653069 | `73c7f7fbf573c542767a4146297e4954369485077ae3880395445af787de0915` | 2026-11-09 |
| fmt | `7b273fbb541c7c9058800bc613f4291274b2d26e` (dynarmic submodule) | `codeload.github.com/fmtlib/fmt` | 705729 | `4d5465ace5ee6aed8183b0954f9f22fffa5ca7dd550fbd5763a54e58ab958bca` | 2026-11-09 |
| mcl | `5fc4beaf331037649b10625736b41365defb4f50` (submodule, fork `azahar-emu/mcl`) | `codeload.github.com/azahar-emu/mcl` | 25681 | `36a2200e7940838112c9967388685a5178c2e9a110233193b914f54b2e2f51fd` | 2026-11-09 |
| oaknut | `94c726ce0338b054eb8cb5ea91de8fe6c19f4392` (submodule) | `codeload.github.com/merryhime/oaknut` | 88547 | `c404db82002f9a851a412b3d55ac9be8a42a2e319fd2fc8a73e63e6ef5d4f97b` | 2026-11-09 |
| robin-map | `054ec5ad67440fcd65e0497e5a27ef31f53fcc7f` (submodule) | `codeload.github.com/Tessil/robin-map` | 70460 | `a73d3a6b98b7961244981bd89a42fcb4aef63a89d1ec135bbc90d3621b355d27` | 2026-11-09 |
| xbyak | `8c0098b69f95d02dea47efcdd42ab5aa2d03c72d` (submodule, x86_64 only) | `codeload.github.com/herumi/xbyak` | 303502 | `4df36b1b40e12db01f7d6f6161fb00d0618e43104e72bec0aa87996472ffa80a` | 2026-11-09 |
| zycore-c | `0b2432ced0884fd152b471d97ecf0258ff4d859f` (submodule, x86_64 only) | `codeload.github.com/zyantific/zycore-c` | 66038 | `698e4623e661db312eb9da327519f53f0a9dd7fbbfacf02159687bca62369467` | 2026-11-09 |
| zydis | `bffbb610cfea643b98e87658b9058382f7522807` (submodule, x86_64 only) | `codeload.github.com/zyantific/zydis` | 813685 | `0c884f6b9fa6699dd3db580a00753d893b1a9e3e4970300da276d0e24238e23b` | 2026-11-09 |
| Boost headers (Azahar `ext-boost`, Boost 1.90.0) | `6a85c3100499e886e11c87a5c2109eedacea0a61` | `codeload.github.com/azahar-emu/ext-boost` | 3165465 | `5bff0bb6eaa1f215227310d6d34cd53983d88676b9bbf06e10e4b945bc6cd8b9` | 2026-11-09 |
| CMake and Ninja (SDK package `cmake/3.31.6`: CMake 3.31.6, Ninja 1.12.1) | `cmake/3.31.6` | `https://dl.google.com/android/repository/cmake-3.31.6-windows.zip` | Not recorded. The SDK tool deletes the zip. Installed: 3793 files, 54 MB | Tree hash `b9d515b9cdf24f148aa7ec2fa7613cfaa2a3f0c5216e8a7eadfc3d1d2becf8b0` (SHA-256 of `cmake-3.31.6-tree.sha256`, the sorted list of each file) | 2026-11-09 |

- The archives are in `../third_party/archives/`. `../third_party/archives.sha256` holds their hashes, and `build-core` checks them before each build.
- The dynarmic submodules `biscuit` (RISC-V) and `catch` (tests) were not downloaded. The build does not use them.
- dynarmic selection (DES-01): `azahar-emu/dynarmic` is the first choice of the plan. On 2026-11-09 its last push was 2026-09-26. `Vita3K/dynarmic` had a push on 2026-10-31. `lioncash/dynarmic` is archived.
- Install command of the CMake package: `cmdline-tools/latest/bin/android.exe sdk install cmake/3.31.6`.

## Build commands (TASK-E0.3)

- `bash emu/scripts/build-core arm64-v8a` builds dynarmic with the A32 frontend only. It uses NDK r27d, `ANDROID_PLATFORM=21`, `c++_static`, Release, PIC and the two 16 KB linker flags. The output is `work/core/arm64-v8a/install/lib/libdynarmic.a`, `libmcl.a` and `libfmt.a`. The first build had 125 steps and 0 errors.
- `bash emu/scripts/build-bridge arm64-v8a` builds the runner `work/bridge/arm64-v8a/arel-emu`. It needs `build-core` first.
- `bash emu/scripts/build-probe` builds the E0 probe APK `dist/arelemu-probe.apk`. It uses `platforms/android-37.0/android.jar`, `javac --release 11`, `d8 --min-api 21`, `aapt2 link`, `jar`, `zipalign -p -f 16` and `apksigner` with the dev key.

## Tools used again

- NDK r27d (`27.3.13750724`): `../../universal/docs/TOOLS.md`.
- JDK 21, build-tools 36.0.0, platform-tools and Node: as v1.
- The dev key: a copy of `universal/keys/dev.jks` and `dev.pass` in `../keys/`.

## Downloads of Phase E6 (TASK-E6.1)

The PC was on the dorm Wi-Fi (not the hotspot) on 2026-11-12. The total size is 9.0 GB, inside the 10 GB to 14 GB of `PLAN.md` 8.6. The speed was about 7 MB/s. The SDK tool `android.exe sdk install` downloads each package, checks it and deletes the archive. So the table gives the size and the SHA-1 that the SDK repository publishes. `logs/e6/packages.tsv` holds the same data.

| Name | Version | Source | Size (bytes) | Checksum | Date |
|---|---|---|---|---|---|
| `emulator` | 37.2.13 (build 16740729) | `dl.google.com/android/repository/emulator-windows_x64-16740729.zip` | 455344678 | SHA-1 `4d7268aa7787c640ad892844e6c9a5f159e91ad3` (published) | 2026-11-12 |
| `system-images;android-36;default;x86_64` | revision 2 | `dl.google.com/android/repository/sys-img/android/x86_64-36_r02.zip` | 844217077 | SHA-1 `829c076e8ff448a336097ae25a355b495ba36e2c` (published) | 2026-11-12 |
| `system-images;android-35;default;x86_64` | revision 2 | `dl.google.com/android/repository/sys-img/android/x86_64-35_r02.zip` | 782404023 | SHA-1 `2d857d170c0d1b827149565da34b3383e5306f7f` (published) | 2026-11-12 |
| `system-images;android-21;default;x86` | revision 5 | `dl.google.com/android/repository/sys-img/android/x86-21_r05.zip` | 208212529 | SHA-1 `00f0eb0a1003efe3316347f762e20a85d8749cff` (published) | 2026-11-12 |
| `system-images;android-21;default;x86_64` | revision 5 | `dl.google.com/android/repository/sys-img/android/x86_64-21_r05.zip` | 292623982 | SHA-1 `9078a095825a69e5e215713f0866c83cef65a342` (published) | 2026-11-12 |
| `system-images;android-23;default;x86_64` | revision 10 | `dl.google.com/android/repository/sys-img/android/x86_64-23_r10.zip` | 365009313 | SHA-1 `7cbc291483ca07dc67b71268c5f08a5755f50f51` (published) | 2026-11-12 |
| `system-images;android-26;default;x86_64` | revision 1 | `dl.google.com/android/repository/sys-img/android/x86_64-26_r01.zip` | 474178332 | SHA-1 `432f149c048bffce7f9de526ec65b336daf7a0a3` (published) | 2026-11-12 |
| `system-images;android-28;default;x86_64` | revision 4 | `dl.google.com/android/repository/sys-img/android/x86_64-28_r04.zip` | 564792723 | SHA-1 `d47a85c8f4e9fd57df97814ad8884eeb0f3a0ef0` (published) | 2026-11-12 |
| `system-images;android-30;default;x86_64` | revision 11 | `dl.google.com/android/repository/sys-img/android/x86_64-30_r11.zip` | 676379777 | SHA-1 `5e4de3946d46f88856c35efcc4d797b381456347` (published) | 2026-11-12 |
| `system-images;android-33;default;x86_64` | revision 2 | `dl.google.com/android/repository/sys-img/android/x86_64-33_r02.zip` | 694005732 | SHA-1 `d4698590184a21fe1e8754284130ad3006b5fc79` (published) | 2026-11-12 |
| `system-images;android-34;default;x86_64` | revision 4 | `dl.google.com/android/repository/sys-img/android/x86_64-34_r04.zip` | 720747116 | SHA-1 `5f6a249f9bc3b1b4c459b13ce2eb646c9680bed1` (published) | 2026-11-12 |
| `system-images;android-35;google_apis_ps16k;x86_64` | revision 5 | `dl.google.com/android/repository/sys-img/google_apis/x86_64-ps16k-35_r05.zip` | 1696342085 | SHA-1 `d709cf26b375abecfbbf5803afd20749ddfa2865` (published) | 2026-11-12 |
| `system-images;android-30;google_apis;x86` | revision 16 | `dl.google.com/android/repository/sys-img/google_apis/x86-30_r16.zip` | 1240551553 | SHA-1 `a58447e540a8581394dd04ee419c6771d62723d8` (published) | 2026-11-12 |

- The install command: `cmdline-tools/latest/bin/android.exe sdk install "system-images;android-35;default;x86_64"`. The quotes keep the `;` characters.
- The accelerator: `emulator -accel-check` gives `WHPX(10.0.19044) is installed and usable`.
- `scripts/avd` writes the AVD files into `work/avd/`. The emulator is a Windows program, so `ANDROID_AVD_HOME` must be a Windows path (`cygpath -w`).
