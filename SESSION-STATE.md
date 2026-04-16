# Session State — Resume Here

Snapshot for picking up exile-olc6502-HD work in a fresh Claude session.

## End-game ship fly-away (TGD's README note) — investigation

README's bullet "*The ship doesn't fly away when you reach the end game, as the BBC scrolling is being ignored*" traced to:

- Standard ROM `scroll_screen` at `$1F58` writes to CRTC registers 12/13 (screen-memory start-address high/low) via `STY $FE00` then `STA $FE01`. This is how the BBC shifts the displayed framebuffer in hardware.
- `screen_offset` at `$00B0/$00B1` is the game's internal scroll tracker; `scroll_screen` derives the CRTC start-address bytes from it.
- For the end-game sequence the game holds the ship object fixed in world-space and advances `screen_offset` so BBC hardware scrolls the world past it.
- The HD port renders from game state (object arrays, not the BBC framebuffer) and the camera is fixed to the player — so the CRTC writes go to RAM and do nothing visible. Hence no fly-away.

**To fix later:** trap writes to `$FE00/$FE01` in `Bus::write`, track the selected CRTC register and current start-address delta, and during fly-away use that delta as an additional camera offset in `Main.cpp` (alongside the existing `fScrollShiftX/fScrollShiftY`). Or alternatively, detect fly-away by a game flag (e.g. `$081E flooding_state` + player state) and directly read `$00B0/$00B1` as the camera pan.

Mapping CRTC start-address → pixel offset needs the Mode 1 framebuffer layout (40 chars × 8 scanline bytes per char row = 320 bytes/row; 20 KB total). Scroll is typically vertical in Exile's ship-exit.

Not yet implemented — captured here so next session has the trace.

---

## Sound system status (latest as of 2026-04-16)

Two-tier sound now in place — voice samples (Tom Seddon WAVs via SampleManager) plus a real SN76489 chip emulator wired through System VIA. Voice samples solid; SN76489 chip pitch/timing iterating against jsbeeb.

**Files added since last snapshot:**
- `samples/[0-6].wav` — Tom Seddon's voice samples (welcome, ows, oohs, destroy, radio die)
- `SampleManager.{h,cpp}` — WAV loader + AudioToolbox playback, background worker thread, per-sample length-based cooldown
- `SN76489.{h,cpp}` — chip emulator, jsbeeb-equivalent (fractional Float64 counters at output sample rate, unipolar bit×vol output, jsbeeb's volume table `pow(10, -0.1*i) / 4`)
- `BBCSound.{h,cpp}` — owns SN76489 + continuous AudioQueue stream (3 buffers × 512 frames at 44100 Hz int16 mono = ~35 ms latency)

**Bus changes:**
- `Bus.h` adds nullable `BBCSound* sound` member
- `Bus.cpp` intercepts writes to System VIA Port A ($FE4F) and Port B ($FE40); IC32 latch falling-edge on line 0 (sound chip /WE) latches Port A value into chip via SN76489::Write

**Main.cpp changes:**
- Added `SampleManager Samples` and `BBCSound Sound` members
- `Game.BBC.sound = &Sound; Sound.Start()` after init
- Sample debug keys: CTRL+0..6 (NOT F-keys; macOS reserves them for media controls)
- Sample triggers in game loop via PC traps at `SAMPLE_TRAP_*` constants in `Exile.h` (variant-specific). Welcome plays once after a 10-frame settle.
- Fake-IRQ scheduler at top of OnUserUpdate, accumulates real `fElapsedTime`, fires the BBC's IRQ chain ($12A6 → sound block at $1320) at 100 Hz target — needed because we don't run real interrupts (vsync is trapped via `cpu.pc == 0x1F66/$1F99` instead).

**Known sound issues (not yet resolved):**
- User perception: pitch/door-interweave frequency lower than reference BBC emulator (jsbeeb)
- Random "thud noise every few seconds" — unexplained; could be AudioQueue underrun or a periodic write we're mishandling
- After many tweaks (250 vs 500 kHz chip clock, 40-100 Hz IRQ, 22050 vs 44100 Hz output, 35 vs 185 ms latency) and matching jsbeeb's `generate()` line-for-line, still subtly off
- **Recommended next step:** capture audio from a known-good emulator running Exile from `bmain.rom` for the SAME game event (e.g. door open) and FFT-compare with our output. Stop guessing rates from text descriptions.

**Bundle structure (do NOT skip):**
- `Exile.app/Contents/MacOS/exile` = standard variant binary
- `Exile-Sideways.app/Contents/MacOS/Exile-Sideways` = sideways variant binary
- `Exile.app/Contents/Resources/samples/*.wav` (and same in Sideways)
- SampleManager searches CWD/samples then exe_dir/samples then exe_dir/../Resources/samples — works for both terminal and bundle launches
- Always `open Exile.app` (or Sideways) — `./exile-standard` from terminal often fails to activate the GLUT window on macOS Tahoe

**Makefile.mac:**
- Per-variant object dirs `obj-std/` and `obj-side/` so building both can't silently mix .o files
- `make -f Makefile.mac both` — builds both variants, auto-copies into `.app/Contents/MacOS/` if bundles exist
- `make -f Makefile.mac exile-standard` / `exile-sideways` for individual

---

**Working dir:** `/Users/dansimms/Documents/C++ Exile/exile-olc6502-HD`
**Canonical fork:** `github.com/dan-simms1/exile-olc6502-HD` — all commits pushed here.
**Older clone:** `github.com/OxygenBubbles/exile-olc6502-HD` (stale — was just the initial fork).

## Clone + build on another machine

```bash
git clone https://github.com/dan-simms1/exile-olc6502-HD.git
cd exile-olc6502-HD
brew install libpng                            # only dep
make -f Makefile.mac                           # builds ./exile (standard, fully working)

# Sideways (WIP, renders partially — see Pending below):
clang++ -std=c++17 -O3 -flto -Wno-deprecated-declarations -I$(brew --prefix libpng)/include \
        -DEXILE_VARIANT_SIDEWAYS_RAM -c -o Exile.o Exile.cpp
clang++ -std=c++17 -O3 -flto -Wno-deprecated-declarations -I$(brew --prefix libpng)/include \
        -DEXILE_VARIANT_SIDEWAYS_RAM -c -o Main.o Main.cpp
clang++ -std=c++17 -O3 -flto -Wno-deprecated-declarations -I$(brew --prefix libpng)/include \
        -c -o Bus.o Bus.cpp
clang++ -std=c++17 -O3 -flto -Wno-deprecated-declarations -I$(brew --prefix libpng)/include \
        -c -o olc6502.o olc6502.cpp
clang++ -o exile-sideways Bus.o Exile.o olc6502.o Main.o \
        -L$(brew --prefix libpng)/lib -lpng \
        -framework GLUT -framework OpenGL -framework Carbon -pthread -flto
# IMPORTANT: if you switch variants, `rm *.o` first — stale object files baked with the
# wrong address constants were our most-painful bug in this investigation.
```

The repo includes all needed ROM files in tree: `bmain.rom`, `bintro.rom`, `sram.rom`, `srom.rom`, `sinit.rom`, `sinit2.rom`, `snapshot_main.rom`.
**Companion doc:** `LEARNINGS.md` (deeper reference for the TypeScript port and the macOS port specifics).

---

## ✅ Working right now

```bash
cd "/Users/dansimms/Documents/C++ Exile/exile-olc6502-HD"
./exile           # or ./exile-standard — BBC Micro standard, HD render, door visible
```

Loads `bmain.rom` + `bintro.rom` (Tom Seddon's authoritative ROMs) and applies HD patches. Tested visually — horizontal door present, game playable.

```bash
./exile-sideways  # BBC Master sideways-RAM. FULLY PLAYABLE (April 2026).
```

**Enhanced (sideways) status: FULLY PLAYABLE.** Loads clean BeebAsm-built ROMs (no jsbeeb snapshot needed). Game runs at 50fps with correct graphics, objects, and scrolling. Known cosmetic issue: stars don't fill the whole HD-wide screen (enhanced's native star code at `$26F7+` is designed for the narrower BBC Master screen; HD standard has a widescreen-star patch at `$26C8` but enhanced has different code there so the standard patch can't be ported verbatim).

**Key insights this session that unlocked it:**

1. **olc6502.cpp's `ReloactedStackAddress` was firing for sideways** — transparently remapping every `$0860-$0976` object-stack access to `$96xx` via the runtime addressing-mode hook. Since enhanced keeps its stacks at the original `$0860` and we don't run `PatchExileRAM`, `$96xx` was unpopulated zeros, so every object read returned zero. Fixed by gating the switch behind `#ifdef EXILE_VARIANT_SIDEWAYS_RAM`.

2. **Main.cpp was zeroing `$0100-$01FF`** on sideways init — this wiped out real game code (`$01A8 process_actions` called from bank 0, `$01D0 wipe_screen_and_start_game`). Without those routines the CPU walked through zero bytes until hitting SINIT2's decrypt loop at `$64DF` and looped forever. Stopped zeroing; on real BBC the stack page coexists with loaded code because the game keeps its stack shallow.

3. **`DetermineBackground` didn't save/restore ZP** — each per-frame classify call scribbled ZP as scratch, corrupting the game's live ZP state. Added ZP snapshot/restore around the call. Also fixed a `y_ = BBC.cpu.x` typo that was clobbering the Y register on restore.

4. **Vsync trap at `$1F99`** (enhanced wait_for_vsync) in `olc6502.cpp`, gated with `#ifdef`. Mirrors the existing `$1F66` trap for standard — force the BCC not-taken by setting Carry = 1 on fetch.

5. **Added BCD (decimal-mode) support to `ADC`/`SBC`** — not strictly needed for display, but SINIT2's game-state decrypt uses it.

6. **Main.cpp's object draw loop was iterating 128** (HD's expanded slot count for standard) — for sideways we only have 16 slots at `$0860`, so reading `OS_TYPE[16..127]` crossed into `OS_SPRITE`/etc. causing 112 garbage sprites to be drawn every frame. Added `OBJECT_SLOTS` variant constant.

7. **All 4 ROMs loaded** (`sram.rom`, `srom.rom`, `sinit.rom`, `sinit2.rom`) at their assembled addresses — mirrors how standard loads `bmain.rom` + `bintro.rom`. SINIT/SINIT2 are loaded but never executed.

**`PatchEnhancedExileRAM()`** is the peer of `PatchExileRAM` and applies 4 portable HD patches: `$0CA5/$10D2` (plot-off, same address both ROMs), `$0C5B` (HD LDY tweak), `$352D` (enhanced equivalent of standard's `$34C6` sprite-too-tall BCS→CLC CLC), plus IRQ-vector fakes.

**Fresh reviewer: the thing to verify first on a new machine** is whether the sideways binary's baked-in addresses match the `EXILE_VARIANT_SIDEWAYS_RAM` define. Our worst debugging hour came from `Exile.o` being stale from a previous non-sideways build; the binary had `0x9600` (standard) baked in even though `Exile.h`'s ifdef said `0x0860`. Sanity check:
```bash
python3 -c "d=open('exile-sideways','rb').read(); print('0x9600 count:', d.count(bytes.fromhex('0096')), ' 0x0860 count:', d.count(bytes.fromhex('6008')))"
```
For a correctly-built sideways binary, the `$9600` count should be low (just the unreachable `PatchExileRAM` CopyRAM literals) and `$0860` count higher. Always `rm *.o` before switching variants.

---

## Files modified vs upstream `tgd2/exile-olc6502-HD`

| File | Change |
|---|---|
| `Makefile.mac` | NEW — macOS build using GLUT/OpenGL/Carbon + Homebrew libpng |
| `Exile.app/` | NEW — minimal macOS bundle (Info.plist + binary) for proper window activation |
| `bmain.rom`, `bintro.rom`, `sram.rom`, `srom.rom`, `sinit.rom`, `sinit2.rom` | NEW — built from Tom Seddon's BeebAsm sources |
| `Exile.h` | Added `bool bFirstWriteWins`, `LoadExileFromBinary(...)`, `RestoreOldBytesInRange(...)` declarations |
| `Exile.cpp` | Parser fixes (`&XXXX` form + try/catch + first-write-wins flag); added `LoadExileFromBinary` & `RestoreOldBytesInRange`; moved HD code patch `$34C6/$34C7 = 0x18 0x18` from corrections block into `PatchExileRAM`; added `#if 0` regions used during bisection (now mostly restored) |
| `Main.cpp` | Replaced `LoadExileFromDisassembly` call with `LoadExileFromBinary`. Added `#ifdef EXILE_VARIANT_SIDEWAYS_RAM` block to load the SRAM/SROM/SINIT/SINIT2 set and skip `PatchExileRAM` for that variant. Added `BISECT_LO/HI` defines used during diagnosis |
| `exile-disassembly.txt` | Currently the *latest* level7 file. We ALSO have `exile-disassembly-2019.txt` (Wayback snapshot 2019-09-19) and `exile-disassembly-new.txt` (current) and `exile-enhanced.txt` (enhanced/sideways variant) saved alongside |
| `diff_old_bytes.txt` | Generated table of (addr, 2019-byte) pairs for the bisection harness — 249 entries |
| `LEARNINGS.md` | Comprehensive notes for TypeScript port |

`exile-standard` is a saved copy of the working binary in case `make` is rerun without `EXILE_VARIANT_SIDEWAYS_RAM`.

---

## Build commands

```bash
# Standard:
make -f Makefile.mac                                                                 # → ./exile

# Sideways RAM:
clang++ -std=c++17 -O3 -flto -Wno-deprecated-declarations \
        -I/opt/homebrew/opt/libpng/include -DEXILE_VARIANT_SIDEWAYS_RAM \
        -c -o Main.o Main.cpp
clang++ -o exile-sideways Bus.o Exile.o olc6502.o Main.o \
        -L/opt/homebrew/opt/libpng/lib -lpng \
        -framework GLUT -framework OpenGL -framework Carbon -pthread -flto

# After modifying any non-Main file, rebuild Main.o for the variant you're testing.
```

---

## Tools available

- **BeebAsm** built at `/tmp/beebasm/build/beebasm`
  - May be wiped on reboot. Source: `git clone https://github.com/stardot/beebasm`, then `cmake .. && make`.
- **Tom's disassembly** at `/tmp/exile_disassembly/`
  - Patched `disk_conv.py` for Python 3 compat.
  - `make BEEBASM=/tmp/beebasm/build/beebasm` then `python3 disk_conv.py --not-emacs tmp/exileb.ssd` (and exilemc.ssd) to extract ROMs.
  - Both also wiped on reboot — re-clone and re-build, or copy the `.rom` files in this repo.

---

## Pending work

### 1. Sideways-RAM: initialise game state so it actually plays

Current state: window renders (blue clear), main loop alive, safety-cap in `OnUserUpdate` bails out after 20k cycles so the frame returns. Address constants correctly ported (see mapping table below). **Missing:** the game's boot code (`SINIT` @ $7690, `SINIT2` @ $6489) has never been executed, so object/tile/player state is all zeroed → CPU PC walks off into uninitialised RAM and hits `BRK`s → the inner game loop never returns to `$19DA` naturally.

Concrete paths forward:

1. **Run the boot code ourselves.** After `LoadExileFromBinary` for the sideways set, set `BBC.cpu.pc` to SINIT's entry ($7690) and clock the CPU until SINIT returns (look for RTS with stack depth 0). Then set PC to `$19DA` and enter the normal frame loop. Needs the `olc6502` emulator to handle the SROM paging trickery and IRQ vectors — may require extra work.
2. **Capture a post-boot RAM dump.** Use a real BBC Master emulator (b-em, BeebEm, JSBeeb) to load the enhanced ROM, let it boot to the title/ready state, dump the full 64 KB of RAM (plus sideways ROM). Embed that as a C array (like `ram_dump.h` in the non-HD project) and load it instead of SRAM/SROM/SINIT/SINIT2. This is faster and reliable but requires external tooling.
3. **Abandon sideways support.** The HD project's value is rendering the standard ROM; the enhanced ROM already has a larger screen natively. If the goal is only "run standard at HD", keep sideways as a stub and remove it from default builds.

**Enhanced-vs-standard address map already decoded (use when porting more patches):**

| Constant / label | Standard | Enhanced |
|---|---|---|
| `action_keys_pressed` | `$126B` | `$1263` |
| `player_is_completely_dematerialised` | `$19B5` | `$19D9` |
| `main_game_loop` | `$19B6` | `$19DA` |
| `set_colour_zero_to_black` | `$1FA6` | `$1FD9` |
| `STA &fe01` (earthquake) | `$260A` | `$2639` |
| `get_tile_and_set_sprite_variables` | `$2398` | `$23CB` |
| sprite-plot-off patch sites | `$0CA5`, `$10D2` | **same** |
| object stack base `$0860+`, tile check `$0095/$0097` | same | **same** |

Landscape-gen block shifts `+$24`, physics-engine block `+$24`, but later routines drift more due to enhanced-only code.

**Traced crash site (861 instructions into the game loop):** Game executes normally from `$19DA` through `$19DC…$1A2C`, then hits `RTS` at `$1A2E`. The comment at `$1A2E` reads "Leave via update routine" — this is a **jump-via-RTS** dispatch: the caller is meant to have pushed `(handler_addr - 1)` onto the stack and then called into a shared tail. Without boot setup, the stack holds garbage (in our trace the RTS popped `$D208`), so CPU jumps into empty RAM, reads zeros (BRK), and the BRK vector `$FFFE/$FFFF` is also zero so it spirals to PC = `$0000`. The fix is not an address shift — it's that the enhanced-variant object-handler dispatch table at `$028a`/`$02ef`-ish and the running-object variables (`this_object_data`, `this_object_handler`) have never been initialised. Confirms that **boot code must be executed** (option 1) or a **post-boot RAM dump** must be captured (option 2). No addresses to shift.

**Update — ROMSEL/`$FE30` bank-switching implemented:** `Bus.h`/`Bus.cpp` now maintains 16 × 16 KB sideways banks with the active bank selected by writes to `$FE30` (or `$FE34`). `$8000-$BFFF` reads/writes route to `sidewaysBanks[activeBank]` when `bSidewaysPaging` is true. Opt-in; standard HD build leaves it off because it uses `$8000-$BFFF` as flat RAM for its relocated object stacks. `Exile::LoadExileFromBinaryToBank(file, bank, offset)` loads ROM bytes into a specific bank. Main.cpp sideways path enables paging, loads SRAM at `$0100`, loads SROM into bank 0 at offset `$19EC` (= `$99EC - $8000`). With paging on, the crash trace now reaches **much deeper** — execution successfully crosses into sideways bank code at `$A6AA+` before RTSing to bytes in the 6502 stack page (`$01A8+`) that came from SRAM's pre-boot image. Zeroing the stack page doesn't rescue it — the game legitimately needs addresses that SINIT/SINIT2 would have pushed. So the conclusion stands: boot emulation or post-boot dump is still required.

Loading addresses that DID matter (and are now correct in Main.cpp):
- `sram.rom` loads at `$0100` (NOT `$1200` — the `.lea` load-address is the BBC's pre-relocation address; the game code is assembled for `$0100`, same as BMAIN).
- `srom.rom` loads at `$8000` (sideways-ROM slot — on real Master this pages via `$FE30`; our flat-RAM emulator just pre-places it).
- `sinit.rom` / `sinit2.rom` not needed once memory is pre-placed (they're boot code we don't run).

### 2. Fullscreen scaling

Window can be maximised but PGE's canvas stays 1280×720 letterboxed. Quick fix in `Main.cpp`:

```cpp
const bool SCREEN_FULLSCREEN = true;     // ask GLUT for true fullscreen
// or bump SCREEN_WIDTH/HEIGHT to native panel and reduce SCREEN_ZOOM
```

The "right" long-term fix is to retire Apple GLUT (deprecated) and switch PGE to SDL2 or a native Cocoa+OpenGL/Metal backend.

### 3. Push to fork

Nothing has been committed. Once happy, commit and push to `OxygenBubbles/exile-olc6502-HD`. Suggested branches:
- `macos-build` — Makefile.mac + Exile.app + parser fixes (clean PR-able subset)
- `binary-loader` — LoadExileFromBinary + bmain/bintro + LEARNINGS.md
- `sideways-ram-stub` — sideways variant + skeleton (mark WIP)

### 4. TypeScript port

User stated this is the eventual goal. Pull learnings from `LEARNINGS.md` §8.

---

## Key debug findings (don't re-derive)

- The level7 text disassembly **is not authoritative** — Tom Seddon's BeebAsm-buildable disassembly is. Current text disassembly has 107 byte regressions vs the real BBC ROM compared to its 2019 snapshot. The missing horizontal door was caused by these regressions.
- The `level7.org.uk/miscellany/exile-disassembly.txt` format changed `#XXXX:` (length 6, colon) → `&XXXX` (length 5, no colon) sometime since 2020. Also the new file shows alternate post-self-modification listings of certain code regions out-of-address-order — naive parsers corrupt them.
- macOS GLUT is deprecated; window activation and fullscreen are fragile. The minimal `Exile.app` bundle + ad-hoc codesign is the workaround for the bouncing-icon-no-window issue on macOS 26.
- `OnUserCreate` runs the 6502 emulator 65 536 times during background-grid generation (~5 s on M5), blocking GLUT's main loop. Causes "no window for 5 s" on first launch; not a hang.

---

## Quick sanity checklist for next session

```bash
cd "/Users/dansimms/Documents/C++ Exile/exile-olc6502-HD"
ls -la *.rom Exile.app exile exile-standard exile-sideways LEARNINGS.md SESSION-STATE.md
make -f Makefile.mac && ./exile        # standard should still work
```

If `bmain.rom` etc. are missing, either:
- Re-clone `/tmp/beebasm` and `/tmp/exile_disassembly`, rebuild, copy the produced files in (see `LEARNINGS.md §4`), or
- The .rom files are in this repo working directory — they should be present.
