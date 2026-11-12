# Don't Starve Together – Linux fixes

One preloaded library for the native Linux version (Steam). Game files are not
modified.

- **Native Wayland** on the SDL from Steam's runtime container (the game's own
  SDL is X11-only), with the game's mouse handling kept correct.
- **No 60 fps cap**, or any cap with accurate frame pacing.
- **No crash on exit** (seen with NVIDIA drivers and MangoHud).

Needs Steam's default Linux runtime for the game; works on any x86-64
distribution. Tested with game version 762172, Intel and NVIDIA, KDE Plasma.

For single-player Don't Starve see
[ds-linux-fixes](https://github.com/fornax-d/ds-linux-fixes).

## Install

```sh
./install.sh
```

It finds the game, builds `dst_fixes.so` (or uses `prebuilt/` without a
compiler) and copies it to `bin64/shims/`. Then set the launch options
(Steam → Don't Starve Together → Properties):

```
LD_PRELOAD='$ORIGIN/shims/dst_fixes.so':"$LD_PRELOAD" %command%
```

Other wrappers go before `%command%`, e.g. `… prime-run mangohud %command%`.
Remove with `./install.sh --uninstall` and clear the launch options.

## Settings

Put them in front, e.g. `DST_FPS_LIMIT=144 LD_PRELOAD=… %command%`.

| Variable | Default | |
|---|---|---|
| `DST_WAYLAND` | Wayland session: 1 | `0`: X11 (XWayland) |
| `DST_FPS_LIMIT` | `0` | `0`: no cap (vsync), `N`: N fps, `game`: original 60 |
| `DST_FAST_EXIT` | `1` | `0`: disable the exit fix |
| `DST_SWAP_INTERVAL` | game's | `0`: vsync off |
| `SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS` | `0` | `1`: minimize fullscreen on focus loss (SDL default) |

The same settings can be changed in game: the **Linux options** mod (Steam
Workshop) adds a Linux tab to Options. It saves them to
`~/.klei/DoNotStarveTogether/<id>/client_save/dst_linux_options`, read at the next start; launch options win.
The library works without the mod; the file stays after the mod is removed,
so delete it (or set the options back first) to return to the defaults.

With vsync a cap only helps at or above the refresh rate. Don't combine it with
MangoHud's `fps_limit`.

## Notes

- **Laptops with two GPUs:** add `prime-run` (NVIDIA) or `DRI_PRIME=1` (AMD).
  `GL_RENDERER` in `~/.klei/DoNotStarveTogether/client_log.txt` shows the GPU.
- **Game doesn't start:** it runs outside Steam's runtime; remove the launch
  options.
- **MangoHud hidden:** a per-game `~/.config/MangoHud/dontstarve_steam_x64.conf`
  with `no_display` starts it hidden.
- **Steam overlay doesn't work on Wayland:** Steam draws it for OpenGL games
  only through GLX and reads input only from X11 (natively it works only under
  gamescope). Use `DST_WAYLAND=0` when you need the overlay.
- **After a game update** that changes the patched code, the fps part prints
  `unknown game build` and leaves the 60 fps cap; the rest keeps working.
- `ERROR: ld.so: object '$ORIGIN/shims/dst_fixes.so' … ignored` comes from
  Steam's helper processes and is harmless.

## How it works

The game links `libSDL2-2.0.so.0` from its `lib64/`. `dst_fixes.so` has that
soname, so it takes its place, and depends on the runtime's
`/usr/lib/x86_64-linux-gnu/sdl2-compat/libSDL2-2.0.so.0` by absolute path
(Steam rewrites `/usr` paths in `LD_PRELOAD` to the host, not inside files).

- `src/sdl.c` – the game was built against a pre-release SDL 2.0.0 without the
  `which` field in mouse events; events are rewritten to that layout, wheel
  events with `y == 0` (read by the game as "up") are dropped, libEGL stays
  loaded (its destructor crashes the render thread at exit).
- `src/fps.c` – the two loads of the 1/60 s frame interval are pointed at our
  value; with a cap, the game's wait (oversleeps, busy-waits) is replaced by
  sleeping to absolute deadlines.
- `src/exit.c` – after the first `SDL_Quit` (settings already saved) the
  process ends with `_exit()`.
- `src/glx.c` – on Wayland the game's GLEW asks for the GLX version of a NULL
  display and then reads uninitialized values; it now gets 1.4.
- `src/config.c` – reads the Linux options mod's file at start and reports
  what the run uses to the mod (through a `dst_linux_status` file that it
  answers from memory; on disk it only says `loaded=0`).

`make` builds into `build/`, `make check` lists the glibc versions and
libraries needed (glibc 2.17 is enough), `make prebuilt` refreshes `prebuilt/`.

## License

MIT, see `LICENSE`. Not affiliated with Klei Entertainment.
