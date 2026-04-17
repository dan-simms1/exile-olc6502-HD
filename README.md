# exile-olc6502-HD

Experimental emulation of [Exile](https://en.wikipedia.org/wiki/Exile_(1988_video_game)) — the 1988 BBC Micro game by Peter Irvin and Jeremy Smith — built on Javidx9's olc 6502 emulator with the [OLC Pixel Game Engine](https://github.com/OneLoneCoder/olcPixelGameEngine) for rendering. Runs at 720p 120 Hz.

**This project is for fun and educational purposes only.** To actually play BBC Exile, please use original BBC hardware or a full BBC emulator such as [BeebEm](http://www.mkw.me.uk/beebem/) or [jsbeeb](https://bbc.godbolt.org/).

## What it does

A 6502 CPU + BBC memory bus + SN76489 sound chip emulated in C++, executing Exile's original ROM code. Three rendering variants selectable at launch:

| Flag | Description |
|---|---|
| `--standard` | Standard BBC Micro (`bmain.rom`). Native BBC framebuffer rendering of the 8 KB screen at `$6000-$7FFF`. |
| `--enhanced` | BBC Master sideways-RAM enhanced (`sram.rom` + `srom.rom`). Native BBC rendering of the 16 KB screen at `$4000-$7FFF` — the taller play-area you only ever saw on a BBC Master. |
| `--hd` (default) | Standard ROM game logic, C++ HD renderer with 128-object expansion and high-resolution sprite/tile/particle rasterisers. |

Optional: `--fullscreen` launches the window fullscreen.

## Build

### macOS

```bash
brew install libpng
make -f Makefile.mac
./Exile.app/Contents/MacOS/exile --hd        # or --standard / --enhanced
```

(The first time, macOS may block the app with a Gatekeeper warning. Run `xattr -cr Exile.app && codesign --force --deep --sign - Exile.app` to clear it.)

### Linux *(experimental)*

Audio currently uses macOS AudioToolbox unconditionally; a cross-platform audio backend port is pending. Graphics (X11 + GL + libpng) and everything else builds fine on Ubuntu 22.04+.

```bash
sudo apt install libpng-dev libglu1-mesa-dev freeglut3-dev libx11-dev libgl1-mesa-dev
make
```

### Windows

Not yet supported. See `.github/workflows/ci.yml` for CI status.

## Controls

Standard Exile BBC key layout (`Z` / `X` / `*` / `?` / `Return`, etc.). Debug and cheat keys:

| Key | Action |
|---|---|
| `1` | Acquire all equipment |
| `2` | Pocket any item |
| `3` | Debug grid overlay |
| `4` | Debug overlay (FPS, object/particle counts, CPU info) |
| `Ctrl+arrows` | Move Finn through walls (dev shortcut) |

## Credits

**Original game**: Peter Irvin and Jeremy Smith (1988). Published by Superior Software. Huge respect — the game's physics, atmosphere, and procedural landscape were far ahead of their time, and I'm still discovering rooms thirty years later.

**This emulator**:
- 6502 core + scaffolding: Javidx9 / OneLoneCoder ([olcNES](https://github.com/OneLoneCoder/olcNES)).
- Rendering: [OLC Pixel Game Engine](https://github.com/OneLoneCoder/olcPixelGameEngine) (Javidx9).
- Exile disassemblies: [Tom Seddon's BeebAsm-buildable version](https://github.com/tom-seddon/exile_disassembly) is the authoritative source for the ROM binaries in this repo. [level7.org.uk](http://www.level7.org.uk/miscellany/exile-disassembly.txt) was the starting point for annotation.
- BBC graphics mode reference: [D.F. Studios](https://www.dfstudios.co.uk/articles/retro-computing/bbc-micro-screen-formats/).
- Voice samples (Tom Seddon): "Welcome to the land of the exile", scream variants, robot death etc.
- Development assistance: Claude Code (Anthropic) — paired with the human on the Mode A/B renderer, the SN76489 port, and the single-binary refactor.

## Known limitations

- **End-of-game ship fly-away** is missing in `--hd` mode: the original game animates the ship leaving via CRTC scroll, which the HD renderer (which draws from extracted game state, not screen memory) doesn't currently honour. Works naturally in `--standard` and `--enhanced` which blit the BBC framebuffer directly.
- **Underwater effect** uses a transparent blue overlay in `--hd` rather than the original XOR palette flip.
- **Sound pitch** is subtly different from jsbeeb — the SN76489 tones are accurate but the envelopes aren't fully modelled.
- **Save/load** is not implemented: the original BBC `*SAVE` + checkpoint system was tied to disc I/O, which we don't emulate.
- **Real VSYNC** isn't delivered; we trap and short-circuit the game's `wait_for_vsync` poll.

## Files

- `Main.cpp` — PGE app shell, keyboard input, game-loop driver, BBC framebuffer renderer.
- `Exile.h/cpp` — game state accessors, HD sprite/tile/particle renderers, ROM patches.
- `Bus.h/cpp` — memory bus, CRTC register intercepts, sideways-RAM paging.
- `olc6502.h/cpp` — 6502 emulator with Exile-specific VSYNC/stack-relocation traps.
- `BBCSound.cpp`, `SN76489.cpp`, `SampleManager.cpp` — audio path.
- `bmain.rom`, `bintro.rom` — standard BBC Micro Exile binaries.
- `sram.rom`, `srom.rom`, `sinit.rom`, `sinit2.rom` — BBC Master enhanced variant binaries.
- `samples/*.wav` — digitised voice samples.
- `exile-disassembly.txt`, `exile-enhanced.txt`, `exile-disassembly-new.txt` — reference disassemblies (not read at runtime).

## License

See LICENSE.
