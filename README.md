# 🔥 POSTAL 1 (1997) — Modern 64-Bit Android Port (`arm64-v8a`)

[![Language](https://img.shields.io/badge/Language-C%2B%2B14%20%7C%20Java-red?style=for-the-badge&logo=cplusplus)](https://isocpp.org/)
[![Android NDK](https://img.shields.io/badge/Android%20NDK-r26%2B-3DDC84?style=for-the-badge&logo=android)](https://developer.android.com/ndk)
[![SDL2](https://img.shields.io/badge/Backend-SDL2-173F5F?style=for-the-badge)](https://www.libsdl.org/)
[![ABI](https://img.shields.io/badge/ABI-arm64--v8a%20(64--bit)-critical?style=for-the-badge&logo=arm)](https://developer.android.com/ndk/guides/abis)
[![16KB Pages](https://img.shields.io/badge/Android%2015%2F16-16%20KB%20Page%20Size-success?style=for-the-badge)](https://developer.android.com/guide/practices/page-sizes)
[![License](https://img.shields.io/badge/License-GPLv2-blue?style=for-the-badge)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)

A native, modern **64-bit (`arm64-v8a`) Android port** of **POSTAL (1997 / Postal Plus)** — the classic isometric action shooter by **Running With Scissors** — built directly from the open-source C++ engine (**RSPiX / WishPiX**) and **SDL2** for contemporary Android flagship and mid-range devices.

> ⚠️ **Important Notice**: This repository contains **source code and build scripts only**. No proprietary game assets (`res/`, `.sak`, `.ini`, audio, or level data) are included. You must own a legitimate copy of **POSTAL: Classic and Uncut** (available on **Steam** or **GOG**) to play.

---

## ✨ Key Technical Features

- 🦾 **Native 64-Bit (`arm64-v8a` / LP64) Compatibility**
  - Fixed legacy 32-bit pointer casts, type widths, Unix/Android path resolution, and CRLF configuration parsing (`RPrefs`) across the original 1997 **RSPiX** and **WishPiX** frameworks so the engine runs natively on modern 64-bit-only ARMv8/ARMv9 SoCs (Google Tensor, Snapdragon 8 Gen series, etc.).
- 📱 **Android 15 / 16 Ready (16 KB Memory Page Size)**
  - Configured ELF segment alignment (`-Wl,-z,max-page-size=16384`) and native library packaging for full compatibility with Android 15/16+ devices enforcing 16 KB kernel page sizes.
- 🎯 **Custom 360° Twin-Stick Touch Overlay**
  - Non-intrusive, semi-transparent Java `View` overlay (`TouchControlsOverlayView`) paired with a low-latency JNI bridge directly into C++ `UInput` (`GetLocalInput()` / `ALLOW_TWINSTICK`) — zero modifications to the original software/SDL renderer:
    - **Left Analog Stick (`MOVE`)**: Smooth 360° analog movement (gentle tilt to walk, full tilt to run), automatically switching to `DPAD` navigation whenever in-game menus are open.
    - **Right Analog Stick (`AIM / FIRE`)**: True 360° independent aiming with an outer `> 78%` crimson **Auto-Fire** zone and visual aim vector.
    - **Full Multi-Touch (`pointerId` tracked)**: Simultaneous running, 360° aiming/shooting, crouching, weapon cycling, and executions.
    - **Ergonomic Action & Service Buttons**: Semicircle combat cluster (`WPN`, `GREN`, `EXEC`, `DUCK`, `SPACE`) plus top utility bar (`ESC`, `ENTER`, `F1` Quick-Save/Next Level, `☠ K` Suicide, and `👁 UI` overlay visibility toggle).
- 📦 **First-Launch Asset Extractor (`FirstRunAssetExtractor`)**
  - Supports optional self-contained builds: if `game_data.zip` (or `res/` + `POSTAL.INI`) is placed inside `app/src/main/assets/` prior to building, the app automatically unpacks all game files into `getExternalFilesDir(null)` on first launch with an on-screen progress indicator and case-sensitive `POSTAL.INI` / `postal.ini` synchronization.

---

## 🎮 On-Screen Controls Reference

| Control | Location | Function |
| :--- | :--- | :--- |
| **Left Stick (`MOVE`)** | Bottom-Left | 360° character movement (auto D-Pad in menus) |
| **Right Stick (`AIM / FIRE`)** | Bottom-Right | 360° aiming (`20%–78%`) + Auto-Fire (`> 78%` outer red ring) |
| **`WPN`** | Right Arc | Cycle owned weapons (`NextWeapon`) |
| **`GREN`** | Right Arc | Quick-select Grenades (`4`) |
| **`EXEC`** | Right Arc | Execute wounded enemies (`X`) |
| **`DUCK`** | Right Arc | Crouch / Dodge incoming fire (`F`) |
| **`SPACE`** | Right Arc | Stand up / Revive / Skip cutscenes (`Space`) |
| **`ESC` / `ENTER`** | Top-Left | Open Pause/Main Menu & Confirm selection |
| **`F1`** | Top-Left | Advance to next level once goal is completed |
| **`☠ K`** | Top-Right | Classic Postal suicide key (`K`) |
| **`👁 UI`** | Top-Right | Show / Hide combat controls while browsing menus |

---

## 📂 How to Play / Where to Place Game Assets

Obtain the original game files from your legal **Steam** or **GOG** installation of *POSTAL: Classic and Uncut* (specifically the **`res/`** folder and **`POSTAL.INI`**).

You can supply the game data using **either** of the two methods below:

### Option A: Copy Directly to Device Storage (Standard Build)
1. Install and launch the APK once (so Android creates the app data folder), then close it.
2. Copy the `res/` directory and `POSTAL.INI` (plus a lowercase copy `postal.ini`) from your PC installation to:
   ```text
   /sdcard/Android/data/org.libsdl.app/files/
   ```
   Using `adb`:
   ```bash
   adb push "res" /sdcard/Android/data/org.libsdl.app/files/res
   adb push "POSTAL.INI" /sdcard/Android/data/org.libsdl.app/files/POSTAL.INI
   adb push "POSTAL.INI" /sdcard/Android/data/org.libsdl.app/files/postal.ini
   ```
3. Recommended `[Input]` / `[Keys]` tweaks inside `postal.ini` for optimal Twin-Stick play:
   ```ini
   UseNewMouse = 0
   UseMouse = 0
   Suicide = K
   ```

### Option B: Bundle `game_data.zip` for a Self-Contained All-in-One APK
1. Pack your `res/` folder and `POSTAL.INI` into the root of an archive named `game_data.zip` (so `res/` and `POSTAL.INI` sit at the top level of the zip).
2. Place `game_data.zip` into:
   ```text
   app/src/main/assets/game_data.zip
   ```
   *(Alternatively, copy `res/` and `POSTAL.INI` directly into `app/src/main/assets/`.)*
3. Build the APK (`./gradlew assembleDebug`). On first launch, `FirstRunAssetExtractor` will automatically unpack everything into `/sdcard/Android/data/org.libsdl.app/files/`.

---

## 🛠️ Build Instructions

### Prerequisites
- **JDK 17** (required by Android Gradle Plugin 8.x)
- **Android SDK** — Compile SDK **34** (`android-34`), Min SDK **24** (`android-24`)
- **Android NDK** — **26.1.10909125** (or newer r26+)
- **CMake** — **3.22.1+**

### Building from the Command Line

1. Clone the repository:
   ```bash
   git clone https://github.com/<your-username>/postal-android.git
   cd postal-android
   ```

2. Point Android SDK in `local.properties` (or set `ANDROID_HOME`) and ensure `JAVA_HOME` points to **JDK 17**:

   **Linux / macOS:**
   ```bash
   export JAVA_HOME=/path/to/jdk-17
   export ANDROID_HOME=/path/to/android-sdk
   ./gradlew assembleDebug
   ```

   **Windows (PowerShell):**
   ```powershell
   $env:JAVA_HOME = "C:\Program Files\Microsoft\jdk-17.0.21.21-hotspot"
   $env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
   .\gradlew.bat assembleDebug
   ```

3. The compiled APK will be generated at:
   ```text
   app/build/outputs/apk/debug/app-debug.apk
   ```

4. Install onto a connected `arm64-v8a` device:
   ```bash
   adb install -r app/build/outputs/apk/debug/app-debug.apk
   ```

---

## 📜 Credits & License

- **Original Game & Engine (`POSTAL`, `RSPiX`, `WishPiX`)**: Created by [**Running With Scissors (RWS)**](https://runningwithscissors.com/). Huge thanks to Running With Scissors for releasing the POSTAL 1 source code to the community!
- **Windowing, Audio & Input Abstraction**: Powered by [**Simple DirectMedia Layer (SDL2)**](https://www.libsdl.org/).
- **License**: The source code in this repository is distributed under the terms of the **GNU General Public License v2.0 (GPLv2)**, matching Running With Scissors' official source release. See `LICENSE` / source file headers for full license details. All POSTAL trademarks, characters, and original game assets remain the sole property of **Running With Scissors, Inc.**
