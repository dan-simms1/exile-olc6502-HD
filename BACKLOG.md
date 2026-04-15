# Backlog

Priority order; only start items below a blocker once the blocker is resolved.

## After enhanced is fully playable

### "Faithful" render mode for standard + enhanced

Drop all the bolt-on C++ sprite/tile/particle reimplementation and render by reading the BBC's own screen memory directly.

**Goal**: Behaviour identical to the BBC Micro standard and BBC Master enhanced originals — same game code paths, same object limits, same sprite/tile drawing routines — but with the graphics engine running at 50/60 fps so scrolling is smooth.

**What stays:**
- Cheats (K1/K2 etc.)
- Variant selection (standard vs enhanced)
- macOS/OpenGL window

**What goes:**
- `GenerateBackgroundGrid()` (65536 CPU runs over classify)
- `DrawExileSprite_PixelByPixel` (custom HD sprite rasteriser)
- `DrawExileParticle` (custom HD particle rasteriser)
- `DrawExileTile*` (custom HD tile rasteriser)
- All the HD-rendering-quality patches (`$34C6`/`$352D`, `$0C5B`, radius, stars widescreen, funny_table)
- Maybe the sprite-plot-off patches at `$0CA5`/`$10D2` — we want the BBC plotter running so the BBC screen memory gets written

**What's needed:**
- Read BBC's native screen memory each frame:
  - Standard BBC Micro: mode 1 screen at `$3000-$7FFF` (256x256 pixels, 4 colours, 2 bits/pixel)
  - BBC Master enhanced: mode 1 or mode 2 in shadow RAM at `$3000-$7FFF` (ACCCON bit controls shadow). 128 KB total, so shadow is separate RAM.
- Decode BBC pixel layout to RGBA (BBC screen packing is non-linear — need a lookup table)
- Proper VSYNC emulation:
  - Advance CPU at ~2 MHz real-time
  - Deliver VSYNC IRQ at 50 Hz
  - Graphics engine reads screen memory at target (50/60 fps) and blits to window
- Keep the full game loop untouched — no forced PC reset each frame, just let the 6502 run at clocked rate.

Essentially this reverts to a textbook BBC Micro emulator (à la JSBeeb) but macOS-native. The "HD" project's hand-ported renderer becomes optional.

---

## Enhanced-port TODOs (dependencies of "Backlog: faithful mode")

- [ ] Game state initialisation (player object + critical vars not populated from clean SRAM alone — SINIT2 runs this but hits copy-protection)
- [ ] VSYNC IRQ emulation (currently trap-and-bypass; faithful mode will need real IRQ)
- [ ] Sound sample relocation (SINIT's move_samples_to_sideways_ram_loop in C++)
- [ ] HD radius bump — enhanced deliberately omits the `INC $9B` that standard has; enhanced may not need it
- [ ] HD stars widescreen — enhanced adds one star per frame at `$26F7+`; HD standard does 9 via `$FF50`. Would need enhanced-specific rewrite.
- [ ] Fullscreen scaling polish (GLUT reshape was removed because it broke non-sideways)
