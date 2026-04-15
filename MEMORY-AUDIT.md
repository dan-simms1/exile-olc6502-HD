# C++ 6502 Memory-Touch Audit — Standard vs Enhanced

Every site in the C++ codebase that READS or WRITES 6502 memory, classified by whether the address has the same meaning in both ROMs.

**Evidence sources:** `exile-disassembly-new.txt` (standard), `exile-enhanced.txt` (enhanced), raw byte compares on `bmain.rom` (std @ $0100) and `sram.rom` (enh @ $0100).

Legend: ✅ = same / correct. ⚠️ = suspect / only matters for one path. ❌ = bug for enhanced.

---

## 1. Exile.cpp — PatchExileRAM() body (STANDARD ONLY — never called in sideways)

All the `P += "..."` lines and `CopyRAM(...)` calls are standard-only — `Main.cpp` only invokes `PatchExileRAM()` in the `#else` (standard) branch. They are NOT a risk for enhanced even when their addresses would collide, because they never execute. Catalogue (status for enhanced is "not run"):

| Block | Addresses | What it does | Enhanced |
|---|---|---|---|
| Object stack relocation | `$0860-$0976` → `$9600-$A700` | CopyRAM the 16-slot stacks to 128-slot area | Not run |
| 21 code-site rewrites | `$1A62`, `$1A8F`, `$1DBE`, `$1E34`, `$1E3C`, `$1EDF`, `$27B7`, `$4BFB`, `$2A7E-$2BB0`, `$3CED`, `$FF10`, `$FF20`, `$FF30`, `$FF40`, `$FE00/FE10/FE20/FE30` | Rewrite instructions to use new stack bases | Not run |
| 128-object loop bounds | `$1E11`, `$1E29`, `$1E70`, `$1EB8`, `$288E`, `$2A78`, `$2A93`, `$2AFB`, `$3442`, `$3C4D`, `$3C51`, `$609E` | `#$10` → `#$80` | Not run |
| Particle restructure | `$202B-$2229` + new code at `$9000/$9200/$9300/$9400` | Reorganise particle stacks for more particles | Not run |
| Stars widescreen | `$26C8-$26E3` (9× `JSR $FF50`), new `$FF50` routine at `$FF5E` | Wider star coverage | Not run |
| Radius bump | `$1143F`, `$1145` (INC $9B) | More sprite radius | Not run |
| `$0C5A LDY #$0A` | `$0C5A` | HD iteration tweak | Not run here (but DONE inline for sideways) |
| `$34C6/$34C7 = $18 $18` (BCS → CLC CLC) | `$34C6` | HD sprite-too-tall bypass | Not run here (sideways does `$352D` inline) |
| `$0CA5 JMP $0CC0`, `$10D2 JMP $10ED` | plot-off | Both | Not run here (DONE inline for sideways) |
| `$19A7: 01 0C 04` funny_table | `$19A7` | Data tweak | Not run (enh equivalent at `$19CB` would need port) |
| `$FF30`/`$FF50` new code | created by patch | New C code | Not run |

**Key fact**: since `PatchExileRAM` is gated behind `#ifndef EXILE_VARIANT_SIDEWAYS_RAM` in `Main.cpp`, nothing in this block writes enhanced memory. The **only** HD patches that reach sideways are the 4 applied inline in `Main.cpp`'s `#ifdef EXILE_VARIANT_SIDEWAYS_RAM` block (see §4 below).

---

## 2. Exile.cpp — Global / Initialise / shared code (RUNS FOR BOTH VARIANTS)

| Line | Code | Address(es) | Std meaning | Enh meaning | Status |
|---|---|---|---|---|---|
| 40 | Parser writes byte at `nRam` | whatever the disassembly line specifies | only runs in `LoadExileFromDisassembly` path (not sideways, not binary load) | — | ✅ N/A |
| 54 | `BBC.ram[loadAddr + i] = buf[i]` | caller-controlled | Writes binary file bytes at caller's address | — | ✅ variant-neutral |
| 101 | `BBC.ram[$34C6/$34C7] = $18` | `$34C6` | level7-disassembly correction — Disassembly path only (Sideways uses binary load) | — | ✅ never runs in sideways |
| 102 | `BBC.ram[$492E/$492F/$4930]` | `$492E-$4930` | level7-disassembly correction | — | ✅ disassembly-only |
| 110 | `CopyRAM` helper | caller-controlled | used by `PatchExileRAM` only | — | ✅ |
| 414-417 | `BBC.ram[$FFA0..$FFA3] = JSR classify; TAY` | `$FFA0-$FFA3` | Our own trampoline. Both ROMs have 0x00 at $FFA0 initially. Target address comes from variant-switched `GAME_RAM_GRID_CLASSIFY` (`$2398` std / `$23CB` enh) | ✅ correct for both |
| 435-436 | `BBC.ram[$0095] = x; BBC.ram[$0097] = y` | `$0095, $0097` | ZP `square_x/tile_x`, `square_y/tile_y` | Same ZP meaning in enhanced (`tile_x` at `$95`, `tile_y` at `$97`) | ✅ |
| 452-458 | Reads `$08, $09, $73, $4F, $51, $53, $55, $75` | all ZP | Standard ZP — tile_id, orientation, palette, x/y fractions/integers, sprite id | Enhanced uses SAME ZP layout (verified in enhanced disassembly) | ✅ |
| 489 | `BBC.ram[a] = v` | any | `RestoreOldBytesInRange` — only called via BISECT debug flags in Main.cpp. Not in default sideways build | — | ✅ |
| 577-578 | Reads `PALETTE_PIXEL_TABLE + i`, `PALETTE_VALUE_LOOKUP + i` | `$1E48`/`$0B79` (std) · `$1E7B`/`$0B79` (enh) | Palette decoding | Variant-switched in `Exile.h` | ✅ |
| 682-684 | ZP save/restore around `GenerateBackgroundGrid` | `$00-$FF` | Protects ZP from classify scratch writes | ZP is ZP, same both | ✅ |
| 686-687 | `cpu.stkp = $FF; cpu.pc = GAME_RAM_STARTGAMELOOP` | — | Reset state before game loop | `STARTGAMELOOP` variant-switched (`$19B6` std / `$19DA` enh) | ✅ |
| 694-715 | `Object()` / `Particle()` read `OS_*` / `PS_*` | `$0860-$A500` (std) / `$0860-$2907` (enh) | Object/particle state | All base constants variant-switched in `Exile.h` | ✅ |
| 761-780 | `DetermineBackground` — snapshot/restore cpu state; write ZP `$0095/$0097`; run classify | `$FFA0` trampoline + ZP | Same mechanism as grid gen | ✅ same as 414-436 |
| **792** | `BBC.ram[$0832 + 1]` (water_level_high[1]) | `$0832+` | water_level table 4 entries | Bytes IDENTICAL in enh (`ce df c1 c1`) | ✅ |
| **793** | `BBC.ram[$082E + 1]` (water_level_low[1]) | `$082E+` | water_level_low table | Bytes IDENTICAL in enh (`00 00 00 00`) | ✅ |
| **798** | `BBC.ram[$14D2 + i]` (x_ranges) | `$14D2` std, **`$14E7` enh** | Water-band x-boundaries | Enhanced `$14D2` = `game_paused` flag byte (diff bytes). **Enhanced x_ranges is at `$14E7`** (confirmed: bytes `00 54 74 a0` match) | ❌ **BUG — wrong address for enhanced** |
| 801 | `BBC.ram[$0832 + i]` (water_level[i]) | `$0832+` | — | Same in both | ✅ |
| 802 | `BBC.ram[$082E + i]` (water_level_low[i]) | `$082E+` | — | Same in both | ✅ |
| 813 | Cheat fills `$0806-$0817` with `$FF` | `$0806-$0817` | `OBJECT_CYAN_YELLOW_GREEN_KEY` + 17 sibling keys/equipment | Same addresses/meanings in enh (first entry `$0806 OBJECT_CYAN_YELLOW_GREEN_KEY` in both) | ✅ |
| **818-819** | Cheat writes `$34C6/$34C7 = $18` | `$34C6` | std: BCS sprite-too-tall (target of HD patch) | enh: BCS angle-check (completely different routine). Enhanced equivalent is at **`$352D`** | ❌ **BUG in sideways — cheat pokes wrong addr (only triggered by K2 key)** |

---

## 3. Exile.cpp — Cheat/SpriteSheet/Draw paths

### DrawExileSprite / DrawExileSprite_PixelByPixel reads

| Function | Reads | Address | Std | Enh | Status |
|---|---|---|---|---|---|
| `GenerateSpriteSheet` | `BBC.read(SPRITE_BITMAP_BASE..END)` | `$53EC-$5E0C` std / `$B3EC-$BE0C` enh | Sprite bitmap | Variant-switched in `Exile.h`. Uses `BBC.read()` so paging honoured in enhanced | ✅ |
| `DrawExileSprite` | `BBC.read(SPRITE_WIDTH/HEIGHT/OFFSET_A/B_LOOKUP + id)` | `$5E0C-$5F83` std / `$BE0C-$BF83` enh | sprite dimension lookup tables | Variant-switched in `Exile.h` | ✅ |

---

## 4. Main.cpp — sideways init block (ONLY RUNS WHEN EXILE_VARIANT_SIDEWAYS_RAM)

| Line | Write | Purpose | Status |
|---|---|---|---|
| — | `bSidewaysPaging = true; activeBank = 0` | Enable ROMSEL emulation | Our own emulator state |
| — | `LoadExileFromBinary("sram.rom", $0100)` | Load SRAM at $0100 | Bytes from file — correct |
| — | `LoadExileFromBinaryToBank("srom.rom", 0, $19EC)` | Load SROM into bank 0 | Bytes from file — correct |
| — | Zero `$0100-$01FF` | Zero 6502 stack page (SRAM overlaps it) | Needed because SINIT would have done this |
| — | `ram[$FFF0] = $40 (RTI)` | Fake IRQ return | Our trap |
| — | `ram[$FFFA-$FFFF] → $FFF0` | Aim all 6502 vectors at the RTI | Our trap |
| — | `ram[$0CA5..$0CA7] = JMP $0CC0` | Skip BBC object plotter | Same addr both ROMs — ✅ |
| — | `ram[$10D2..$10D4] = JMP $10ED` | Skip BBC tile plotter | Same addr both ROMs — ✅ |
| — | `ram[$352D..$352E] = $18 $18` | Enhanced equivalent of std `$34C6` BCS → CLC CLC | ✅ verified |
| — | `ram[$0C5B] = $0A` | HD `LDY #$04 → #$0A` tweak | Same addr both ROMs — ✅ |

**Missing for parity with standard's `PatchExileRAM`:**
- [ ] Sample relocation from main-RAM `$1984-$5883` into a sideways bank (SINIT's `move_samples_to_sideways_ram_loop`)
- [ ] Radius bump: standard has `INC $9B` × 2 at `$111F/$1121` naturally. Enhanced has **none** (grepped — zero occurrences of `e6 9b`). Need to find where enhanced handles sprite radius, probably at a different ZP var or address.
- [ ] Stars widescreen: standard `$26C8-$26E3` rewrite is purely standard-only; enhanced star code is at `$26F7+` with different structure (worm-spawning preamble at `$26C8`). Would need enhanced-specific widening.
- [ ] funny_table: enhanced copy is at `$19CB` — contents already IDENTICAL to standard (`01 0c 04 00 00 02`), so patch is a no-op.

---

## 5. Main.cpp — per-frame hot path

| Line | Access | Addr | Notes |
|---|---|---|---|
| ~170 | `GetKey(...)` then `ram[OS_Y]--` etc. | `OS_Y` variant-switched | ✅ |
| ~219 | `cpu.pc = GAME_RAM_STARTGAMELOOP` | variant-switched | ✅ |
| ~232-236 | Per-instruction probes: `cpu.pc == 0x1F93`? etc. | Standard-specific breakpoints `$1F93` / `$19DA` | ⚠️ Check: breakpoints in sideways block target enhanced addresses; in standard block target std addresses. Verify. |
| ~264, 267 | `ram[GAME_RAM_INPUTS + nKey]` | variant-switched | ✅ |
| ~280 | `ram[GAME_RAM_PLAYER_TELEPORTING]` | variant-switched | ✅ |
| ~339 | `ram[GAME_RAM_PARTICLE_COUNT]` | variant-switched | ✅ |

---

## 6. Bus.cpp

No direct `BBC.ram[]` writes — only `ram[addr] = data` on the Bus object itself (different thing). ROMSEL ($FE30/$FE34) handling is implemented. No variant-specific writes. ✅

---

## 7. olc6502.cpp runtime trap

| Line | Trap | Std | Enh | Status |
|---|---|---|---|---|
| 122 | `if (pc == $1F66) SetFlag(C, 1)` — bypass std wait_for_vsync | `$1F66` | **bypassed by `#ifdef EXILE_VARIANT_SIDEWAYS_RAM`**; enhanced gets `$1F99` instead | ✅ |

---

## Summary of bugs requiring fix (for sideways build):

1. **`$14D2` water x_ranges** (Exile.cpp:798) — add `GAME_RAM_X_RANGES` variant-switched constant (`$14D2` std, `$14E7` enh).
2. **`$34C6` in `Cheat_StoreAnyObject`** (Exile.cpp:818-819) — wrong addr for enhanced. Variant-switch to `$352D`.
3. **Missing sample relocation** — enhanced expects sound samples at `$8100-$BFFF` of a sideways bank, copied from `$1984-$5883`.
4. **Missing radius bump in enhanced** — need to find where enhanced manages sprite radius and add the HD bump (address unknown).
5. **Missing stars widescreen for enhanced** — enhanced's `$26F7+` star code needs HD widening (different structure from standard's `$26C8`).

Items 1-3 are concrete fixes. Items 4-5 need more enhanced-ROM research first.
