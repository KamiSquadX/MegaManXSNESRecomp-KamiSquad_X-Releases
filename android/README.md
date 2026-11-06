# Android (APK)

The Android build is the same game and mods as the desktop build: the native
host compiled as `libmain.so` with SDL3, run by SDL3's own `SDLActivity`. It
targets arm64-v8a (phones, handhelds) and x86_64 (the Android emulator),
Android 9 (API 28) and later.

## Playing

1. Install the APK.
2. On first launch, select your own **Mega Man X (USA)** ROM (`.sfc` or `.smc`,
   checked by SHA-256). Optionally select your **Mega Man X3 (USA)** ROM for Zero,
   co-op and the X3 weapons.
3. **Play** opens the usual launcher (Mods, Settings, Netplay).

To pick the ROMs again later, long-press the app icon and choose **ROM setup**.
Everything the game writes (config, saves, mod settings, replays and
`logs/android-session.log`) lives in `Android/data/com.kamisquadx.megamanxrecomp/files`.

## Controls

**On-screen Gamepad** is player 1's default input source on Android. It draws a
D-pad, A B X Y, L R, L2 R2, Start and Select over the game, plus a **MENU** button at the top
that opens the launcher. In the launcher, go to **Settings → Controllers →
Configure**. With **Input source** set to **On-screen Gamepad**, the **INPUT
BINDINGS - PLAYER 1** card lets you:

- choose which controls show (L3, R3 and the L-Stick and R-Stick are off by
  default; any control can be hidden);
- set their size (50–200%) and opacity (10–100%);
- swap sides (left-handed);
- bind each on-screen button like a controller's.

A Bluetooth/USB controller or a keyboard still works. Pick **Gamepad** or
**Keyboard** as the input source.

In the launcher, touch works like a mouse:

- tap to click;
- drag a list to scroll it;
- hold a control to see its tooltip;
- tap a text box to open the system keyboard.

**Back** closes a dialog or page. On the dashboard it closes the launcher (back to the
game when it was opened in-game). In the game, **Back** opens the launcher.

## Building

Needs Android SDK 34, NDK 27.0.12077973, CMake 3.22.1 (SDK), a JDK 17+ (Android
Studio's), Python 3, and a desktop CMake with a C compiler for the build-time
tools (`host-tools/`). The generated game code in `src/gen` must exist, as for a
desktop build.

```
cd android
gradle assembleRelease -PhostCmake="C:\Program Files\CMake\bin\cmake.exe"
```

`fetch_sdl3.py` downloads the SDL3 release the runner pins. Set
`MMX_ANDROID_KEYSTORE` (and its password variables) to sign a release with a
stable key; otherwise the local debug key signs it.
