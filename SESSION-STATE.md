# Session State — Resume Here

Snapshot for picking up exile-olc6502-HD work in a fresh Claude session.

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
./exile-sideways  # BBC Master sideways-RAM. Renders some game elements from jsbeeb snapshot.
                  # Game freezes at wait_for_vsync ($1F93) — last-valid state stays on screen.
```

**Where sideways got to in this session (April 2026):**
1. Load 64 KB post-boot RAM snapshot captured from jsbeeb (`snapshot_main.rom`) instead of trying to reproduce SINIT/SINIT2. snapshot_main.rom is committed in the repo.
2. Initialise sideways-RAM banks to `0xFF` in `Bus::Bus()` (matches real empty ROM sockets); without this, stray reads returned 0x00 = BRK and spiralled PC → 0.
3. Patch `$FFFE/FFFF` / `$FFFA/B` / `$FFFC/D` → `$FFF0` which holds an RTI. Any stray BRK returns cleanly.
4. Snapshot-and-restore zero-page around `GenerateBackgroundGrid` AND per tile iteration (enhanced classify at `$23CB` writes scratch to ZP that corrupts live game state). Hung tiles `continue` instead of aborting the whole grid.
5. Made object/particle/particle-count addresses variant-switchable in `Exile.h`:
   - `OS_TYPE/SPRITE/X_LOW/X/Y_LOW/Y/FLAGS/PALETTE/TIMER` — sideways uses un-relocated `$0860+` (because we don't run `PatchExileRAM`), standard uses relocated `$9600+`.
   - `PS_X_LOW/X/Y_LOW/Y/TYPE` + `PS_STRIDE` — sideways is interleaved `$2907 + N*8`, standard is `$8800 + N` after HD relocation.
   - `GAME_RAM_PARTICLE_COUNT` — `$1E8B` (sideways) vs `$1E58` (standard HD-relocated).
   - `Exile::Particle()` now scales by `PS_STRIDE`, `Exile::Object()` uses `OS_*` constants. Same code compiles both variants.

**Current state (as of session end):** game renders the player ship but tiles look wrong. Freezes at `wait_for_vsync` `$1F93` because we don't emulate VSYNC IRQ. Attempting to fake vsync by poking `$11DC = 2` OR triggering `cpu.irq()` caused visible regression (end-of-frame code corrupts rendering); reverted. Safety cap of 5M instructions prevents UI hang.

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
