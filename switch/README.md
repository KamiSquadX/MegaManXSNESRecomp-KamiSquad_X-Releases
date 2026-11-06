# Nintendo Switch (.nro)

The Switch build is the same game, launcher and mods as the desktop build. It runs
as a homebrew `.nro` from the Homebrew Menu (hbmenu) on a Switch running custom
firmware.

## Installing

1. Extract `MegaManXSNESRecomp-switch-<version>.zip` to the root of your SD
   card. This creates `switch/MegaManXSNESRecomp/` with the `.nro`, `assets/`,
   `mods/` and `config.ini`.
2. Copy your own **Mega Man X (USA)** ROM (`.sfc` or `.smc`) into that folder,
   or pick it later with **Browse For ROM**. You can also add **Mega Man X3 (USA)**
   for Zero, co-op and the X3 weapons. No ROMs are included.
3. Start **Mega Man X Recomp** from the Homebrew Menu. Launch it through title
   override (hold R while starting a game), not the album applet, so it gets
   full memory.

Everything the game writes stays in `sdmc:/switch/MegaManXSNESRecomp/`:
config, saves, mod settings, replays and `logs/switch-session.log`.

## Controls

Player 1 uses the controller in player 1's slot by default: handheld Joy-Cons,
a Joy-Con pair, a Pro Controller or a Switch SNES controller. Game buttons
follow the SNES layout, so A, B, X and Y sit where they do on a SNES pad.

In the launcher:

- the D-pad moves between controls;
- **A** selects and **B** goes back (to the dashboard, or from the in-game
  dashboard back to the game);
- touch works like a mouse: tap to click, drag lists to scroll, hold a control
  to read its tooltip;
- text boxes open the Switch keyboard;
- **Browse For ROM** opens a built-in SD card browser; **Home** jumps to the
  card's root.

In the game, press **−** and **L-stick press** together (Select+L3) to open the
launcher.

**On-screen Gamepad** is also available as an input source. Pick it in
**Settings → Controllers → Configure**. It draws touch controls over the game
with a **MENU** button, and the **INPUT BINDINGS - PLAYER 1** card picks which
controls show, their size, opacity and handedness.

## Not yet on Switch

- Netplay: the network library isn't ported to the Switch's sockets yet.
  Local co-op works.

## Building

Needs devkitPro with `switch-dev`, `switch-sdl2` and `switch-mesa`, a Windows or
Linux Python 3, and a desktop build of `mmx_saber_sfx_convert` (the Android
`host-tools` build makes one). The generated game code in `src/gen` must exist,
as for a desktop build. From devkitPro's MSYS2 shell:

```
cmake -S . -B build-switch -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
  -DSNESRECOMP_SDL_BACKEND=SDL2 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DSNESRECOMP_BUILD_VERSION=<version> \
  -DMMX_SABER_SFX_CONVERTER=<path to mmx_saber_sfx_convert>
make -C build-switch -j12 all
```

This produces `build-switch/MegaManXSNESRecomp.nro`, with `assets/`, `mods/`,
`config.ini` and `logging.ini` staged beside it.
