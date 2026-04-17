# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This is an experimental BBC Micro Exile emulator written in C++, running at 720p 144Hz. It's a portable, high-resolution reimplementation of the classic BBC Micro game using the OLC Pixel Game Engine for graphics. The emulator includes a 6502 CPU emulator, BBC Micro memory bus, SN76489 sound chip emulation, and sample playback.

## Build Commands

### macOS (recommended platform for this project)
```bash
brew install libpng          # If not already installed
make -f Makefile.mac         # Produces ./exile binary
xattr -cr Exile.app && codesign --force --deep --sign - Exile.app  # Sign the app bundle
./Exile.app/Contents/MacOS/exile  # Run the app
```

### Linux
```bash
make clean && make           # Uses g++ with X11, OpenGL, libpng
./exile                      # Run the binary
```

## Important Build Notes

- **macOS window activation**: Running the binary directly may cause the window to fail to activate on newer macOS versions. The `Exile.app` bundle with proper `Info.plist` and codesigning fixes this.
- **Disassembly file**: The `exile-disassembly.txt` file is downloaded automatically by the Makefile but must be in the project root to run the emulator. It contains the BBC Micro ROM code that gets loaded into memory.
- **ROM variants**: The project supports two ROM variants:
  - **Standard BBC Micro** (`-DEXILE_VARIANT_SIDEWAYS_RAM=0`): Original ROM with HD rendering enhancements
  - **BBC Master enhanced** (default): Compiled with sideways RAM support for the enhanced ROM variant

## Project Architecture

### Core Components

**Exile.h/cpp** — Game logic coordinator
- Parses the Exile ROM disassembly and loads it into emulated memory
- Manages game state: objects, particles, background tiles, sprites
- Patches the original BBC code for HD rendering (sprite/tile/particle rasterizers)
- Reads game state from 6502 memory and renders to screen
- Generates sprite sheets and background grids from original BBC data

**Bus.h/cpp** — Memory and I/O interface
- 64 KB main RAM
- Optional sideways-RAM banks (16 banks × 16 KB for BBC Master enhanced)
- Routes CPU reads/writes, handles ROM paging via ROMSEL ($FE30)
- Forwards sound writes to BBCSound

**olc6502.h/cpp** — 6502 CPU emulator
- Complete 6502 instruction set
- Trap points for sample playback (`SAMPLE_TRAP_*` and `SOUND_TRAP_PLAY_SOUND`)
- Trap points for emulator control (screen flash, earthquakes, game loop)

**Main.cpp** — Graphics and input layer
- OLC Pixel Game Engine wrapper for rendering, input, and main loop
- Coordinates Exile game logic with graphics output
- Handles camera/viewport positioning for scrolling
- Input mapping (BBC keyboard layout to host OS keys)
- Debug overlays (cheat codes 1–4)

**BBCSound.h/cpp & SampleManager.h/cpp** — Audio
- SN76489 sound chip emulation (generates tones)
- Sample playback triggered by PC traps in the game code
- Tom Seddon WAV samples for scream, robot death, etc.

### Key Addresses (Memory Layout)

The project defines separate memory layouts for standard and enhanced ROMs (see `Exile.h`):

**Standard BBC Micro**:
- Object stacks relocated to `$9600+` with 128-slot layout
- Particle stacks at `$8800+`, `$8900+`, `$8a00+`, `$8b00+`, `$8d00+`

**BBC Master Enhanced**:
- Object stacks at `$0860+` (original 16-slot layout)
- Particle stacks at `$2907+` (original interleaved format)

Sprite bitmaps, palette tables, and lookup tables follow similar patterns but differ between variants.

### Rendering Pipeline

1. **GenerateBackgroundGrid()** — Pre-renders all 256×256 background tiles to a lookup table by running the classify routine
2. **DrawExileTile*** — Renders cached tiles to screen
3. **DrawExileSprite_PixelByPixel** — Renders game objects/sprites
4. **DrawExileParticle** — Renders particle effects
5. All coordinates are in game space; screen projection is handled by `ScreenCoordinateX/Y()`

## Disassembly Parsing

The project parses `exile-disassembly.txt` (either level7.org.uk or Tom Seddon's format). Critical notes:

- **Two formats supported**: Old format `#0100: …` and new format `&0100 …`
- **Robust hex parsing**: All disassembly tokens are wrapped in try/catch; bad lines are skipped
- **First-write-wins**: The parser loads bytes in order; later duplicates (self-modified code) are ignored. `PatchExileRAM()` calls override this to apply patches
- **Sync with reality**: Tom Seddon's BeebAsm-buildable disassembly (https://github.com/tom-seddon/exile_disassembly) is more authoritative than level7.org.uk's text disassembly, which has ~107 byte regressions

## Sound Emulation

Sound is triggered in two ways:

1. **Sample playback**: PC traps at `SAMPLE_TRAP_*` addresses (scream, robot dies) → `SampleManager.Play()`
2. **Tone synthesis**: PC trap at `SOUND_TRAP_PLAY_SOUND` → reads the 4-byte envelope block after the JSR and synthesizes a square wave via `SampleManager.PlayTone()`

The SN76489 chip itself is fully emulated (tone + noise + volume), but the BBC's DAC and original sample playback routine are not. Sample WAVs are pre-recorded and triggered by C++ instead.

## Cheats & Debug

- **Key 1**: Acquire all equipment
- **Key 2**: Pocket any item
- **Key 3**: Debug grid overlay
- **Key 4**: Debug overlay (FPS, object/particle counts, CPU info)

## Known Limitations & TODOs

- Disassembly downloader (Makefile) targets level7.org.uk, which has accumulated bugs; consider switching to Tom Seddon's binary
- `VSYNC` and game-loop timing use trap-and-bypass; faithful emulation would need real IRQ delivery
- macOS fullscreen/reshape handling is disabled on M-series due to GLUT crashes; use native green button
- Underwater rendering uses transparent blue overlay instead of XOR
- Enhanced ROM port has incomplete game-state initialization (SINIT2 hits copy protection)

## Further Reading

- **BACKLOG.md**: Roadmap for "faithful render mode" (screen-memory reads instead of custom rasterizers)
- **LEARNINGS.md**: Detailed notes on macOS build, disassembly format quirks, ROM differences
- **PATCH-INVENTORY.md**: List of all patches applied to Exile RAM
- **MEMORY-AUDIT.md**: Investigation of memory layout and object/particle stacks
- **CREDITS.md**: Attribution and references
- **SESSION-STATE.md**: Current session snapshot for continuity across machines
