# 🔥 POSTAL 1 (1997) — Modern 64-Bit Android Port (`arm64-v8a`)

## 📥 [👉 DOWNLOAD LATEST APK HERE (Releases) 👈](https://github.com/SDKflux11/Postal1-android/releases/latest)

[![Language](https://img.shields.io/badge/Language-C%2B%2B14%20%7C%20Java-red?style=for-the-badge&logo=cplusplus)](https://isocpp.org/)
[![Android NDK](https://img.shields.io/badge/Android%20NDK-r26%2B-3DDC84?style=for-the-badge&logo=android)](https://developer.android.com/ndk)
[![SDL2](https://img.shields.io/badge/Backend-SDL2-173F5F?style=for-the-badge)](https://www.libsdl.org/)
[![ABI](https://img.shields.io/badge/ABI-arm64--v8a%20(64--bit)-critical?style=for-the-badge&logo=arm)](https://developer.android.com/ndk/guides/abis)
[![16KB Pages](https://img.shields.io/badge/Android%2015%2F16-16%20KB%20Page%20Size-success?style=for-the-badge)](https://developer.android.com/guide/practices/page-sizes)
[![License](https://img.shields.io/badge/License-GPLv2-blue?style=for-the-badge)](LICENSE)

A native **64-bit (`arm64-v8a`) Android port** of **POSTAL 1 (1997 / Postal Plus)** — the classic isometric shooter by **Running With Scissors** — powered by **SDL2** and **Android NDK** for modern Android phones and tablets.

> ⚠️ **Legal Notice**: This repository contains **source code only**. Proprietary game data (`res/`, `.sak`, `POSTAL.INI`, audio, and levels) is **not** included. You need a legitimate copy of *POSTAL: Classic and Uncut* from **Steam** or **GOG** to play.

---

## ✨ Key Features

- 🦾 **Native 64-Bit (`arm64-v8a` / LP64) Support**: Patched legacy 32-bit pointer arithmetic, data type sizes, Unix/Android working directory resolution, and CRLF config parsing across the original **RSPiX / WishPiX** C++ engine.
- 📱 **Android 15 / 16 Ready (16 KB Page Size Alignment)**: Compiled with `-Wl,-z,max-page-size=16384` ELF segment alignment for full compatibility with modern Android 15/16+ devices.
- 🎯 **Custom 360° Twin-Stick Touch Controls**:
  - **Left Analog Stick (`MOVE`)**: Smooth 360° movement (tilt gently to walk, push further to run) with automatic `DPAD` navigation inside menus.
  - **Right Analog Stick (`AIM / FIRE`)**: Independent 360° aiming with a crimson **Auto-Fire outer ring (`> 78%`)** and real-time aim ray.
  - **Full Multi-Touch (`pointerId`)**: Run, aim in 360°, shoot, crouch, and switch weapons simultaneously.
  - **Dedicated On-Screen Buttons**: Ergonomic combat arc (**`WPN`**, **`GREN`**, **`EXEC`**, **`DUCK`**, **`SPACE`**) plus top utility bar (**`ESC`**, **`ENTER`**, **`F1`** Next Level, **`☠ K`** Suicide, and **`👁 UI`** visibility toggle).
- 📦 **First-Launch Auto-Extractor (`game_data.zip`)**: Supports optional self-contained APK builds — if `game_data.zip` is bundled in `app/src/main/assets/`, all game files are unpacked automatically into `getExternalFilesDir(null)` on first launch.

---

## 🎮 Controls Overview

| Control | Position | Action |
| :--- | :--- | :--- |
| **`MOVE` Stick** | Bottom-Left | 360° movement in gameplay / D-Pad navigation in menus |
| **`AIM / FIRE` Stick** | Bottom-Right | 360° aiming (`20%–78%`) & Auto-Fire in outer red zone (`> 78%`) |
| **`WPN`** | Right Arc | Next Weapon (`]`) |
| **`GREN`** | Right Arc | Quick-select Grenades (`4`) |
| **`EXEC`** | Right Arc | Execute wounded enemies (`X`) |
| **`DUCK`** | Right Arc | Crouch / Dodge (`F`) |
| **`SPACE`** | Right Arc | Stand up / Revive / Skip cutscenes (`Space`) |
| **`ESC` / `ENTER`** | Top-Left | Pause / Menu & Confirm |
| **`F1`** | Top-Left | Advance to next level when objective is complete |
| **`☠ K`** | Top-Right | Commit Suicide (`K`) |
| **`👁 UI`** | Top-Right | Toggle combat HUD overlay while browsing menus |

---

## 📂 Installation & Game Data Setup

1. **Download & Install the APK**:
   Grab the latest APK from [**Releases**](https://github.com/SDKflux11/Postal1-android/releases/latest), install it on your Android device, launch it once so Android creates its data folder, and close it.

2. **Copy Game Files from Steam / GOG**:
   Locate your PC installation of *POSTAL 1* (e.g. `Steam\steamapps\common\POSTAL1`) and copy the **`res/`** folder and **`POSTAL.INI`** (plus a lowercase copy named **`postal.ini`**) into:
   ```text
   /sdcard/Android/data/org.libsdl.app/files/
   ```
   Example via `adb`:
   ```bash
   adb push "res" /sdcard/Android/data/org.libsdl.app/files/res
   adb push "POSTAL.INI" /sdcard/Android/data/org.libsdl.app/files/POSTAL.INI
   adb push "POSTAL.INI" /sdcard/Android/data/org.libsdl.app/files/postal.ini
   ```
   *(Recommended settings inside `postal.ini`: set `UseNewMouse = 0`, `UseMouse = 0`, and `Suicide = K`.)*

3. **Optional — Self-Contained `game_data.zip` Build**:
   Before building from source, you can pack `res/` and `POSTAL.INI` at the root of `app/src/main/assets/game_data.zip`. On first launch, the app will automatically unpack the archive into `/sdcard/Android/data/org.libsdl.app/files/`.

---

## 🛠️ Building from Source

### Requirements
- **JDK 17**
- **Android SDK** (API 34) & **Android NDK r26+** (`26.1.10909125`)
- **CMake** (`3.22.1+`)

### Build Commands (Windows PowerShell)
```powershell
git clone https://github.com/SDKflux11/Postal1-android.git
cd Postal1-android

$env:JAVA_HOME = "C:\Program Files\Microsoft\jdk-17.0.21.21-hotspot"
.\gradlew.bat assembleDebug
```
The compiled debug APK will be located at:
```text
app\build\outputs\apk\debug\app-debug.apk
```

---

## 📜 Credits & License

- **POSTAL 1, RSPiX & WishPiX**: Original game and engine by [**Running With Scissors**](https://runningwithscissors.com/), released under the **GNU General Public License v2.0 (GPLv2)**.
- **SDL2**: Windowing, audio, and platform abstraction via [**Simple DirectMedia Layer**](https://www.libsdl.org/).
