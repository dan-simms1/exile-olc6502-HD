# HD Patch Inventory — Standard ↔ Enhanced (Sideways) Mapping

Every patch applied in `Exile.cpp` (`PatchExileRAM()`, `Initialise()`) and `olc6502.cpp`, grouped by purpose, with the enhanced-ROM equivalent when known. Check each one off as it's ported.

Legend:
- ✅ = ported (or N/A — enhanced already behaves this way)
- ⚠️  = partial / needs verification
- ❌ = not yet ported

---

## 1. olc6502.cpp runtime traps

| Trap | Standard PC | Enhanced PC | Purpose | Status |
|---|---|---|---|---|
| VSYNC BCC-force-exit | `$1F66` | `$1F99` | Force Carry=1 so `BCC wait_for_vsync` falls through (we don't emulate VIA vsync IRQ) | ✅ done (guarded by `EXILE_VARIANT_SIDEWAYS_RAM`) |

`ReloactedStackAddress()` in `olc6502.cpp` also redirects certain reads — check whether those are needed for enhanced. (Relate to §2.)

---

## 2. 128-object stack relocation

Purpose: HD build wants ≥128 objects; original 16-entry stacks at `$0860-$0976` don't fit, so `PatchExileRAM` copies each stack base into `$9600-$A700` (16 bytes each, leaving 256-byte pages so a stack fits).

Standard stacks moved to:
| Field | Original | Standard HD new base |
|---|---|---|
| type | `$0860` | `$9600` |
| sprite | `$0870` | `$9700` |
| x_low | `$0880` | `$9800` |
| x | `$0891` | `$9900` |
| y_low | `$08a3` | `$9a00` |
| y | `$08b4` | `$9b00` |
| flags | `$08c6` | `$9c00` |
| palette | `$08d6` | `$9d00` |
| vel_x | `$08e6` | `$9e00` |
| vel_y | `$08f6` | `$9f00` |
| target | `$0906` | `$a000` |
| tx | `$0916` | `$a100` |
| energy | `$0926` | `$a200` |
| ty | `$0936` | `$a300` |
| supporting | `$0946` | `$a400` |
| timer | `$0956` | `$a500` |
| data_ptr | `$0966` | `$a600` |
| extra | `$0976` | `$a700` |
| target_object | `$0a06`? | `$a800` (new stack) |

**Sideways problem:** `$8000-$BFFF` is the ROM-paging window on BBC Master, not flat RAM. We can't drop object stacks there without clashing with sideways bank 7 (SROM). Options:
- **Keep 16 objects at original `$0860+`** → skip lines 117-237 entirely. No code-rewrite patches needed. Loses the 128-object expansion. ← **currently doing this**
- Relocate to a free region in `$0000-$3FFF` or `$CFFF-$DFFF` of the Master's main RAM. Needs new OS_* base constants in `Exile.h` (sideways) AND rewriting every `$08xx,X` reference in the enhanced ROM at code addresses that mostly drift `+$24` vs standard.

**Status:** ✅ acceptable for now (16 objects enough for playable demo). Revisit if HD fidelity requires more.

Code-rewrite sub-patches tied to relocation (all only needed if we relocate):
| Site (standard) | Purpose | Enhanced equivalent | Status |
|---|---|---|---|
| `$1a62 → $ff10` trampoline | read/write `target` + `target_object` | drifts `+$24`? | ❌ not needed while skipping relocation |
| `$1a8f → $ff00` | mask+ASL×4 for object selection | ditto | ❌ |
| `$1dbe → $ff20` | store back target/target_object | ditto | ❌ |
| `$1e34: EOR $a800,X` | target_object XOR | ditto | ❌ |
| `$1e3c → $fe00` self-mod trampoline | object-target update (X form) | ditto | ❌ |
| `$1edf → $fe10` | object-target update (Y form) | ditto | ❌ |
| `$27b7 → $fe20` | object-target update | ditto | ❌ |
| `$4bfb → $fe30` | object-target update | ditto | ❌ |
| `$2a7e-$2bb0` block rewrites | `$9580,Y`/`$9680,Y`/… references | ditto | ❌ |
| `$3ced → $ff40` | zero target/target_object | ditto | ❌ |
| `$1e11/1e29/1e70/1eb8/288e/…` loop bounds `#&80` | expand 16→128 loops | ditto | ❌ |

---

## 3. Particle stack restructure

Purpose: Standard ROM uses 1-byte-per-particle interleaved layout `$86xx-$8Dxx` (`X = particle index`). HD rewrites it to split stacks `$86xx` (vel_x), `$87xx` (vel_y), …, `$8Dxx` (type), each 128 entries deep.

**Enhanced ROM does NOT use this layout at all** — it natively uses an 8-byte-per-particle interleaved layout starting at `$2907` (each particle: `x_frac, y_frac, x, y, ttl, colour_flags, vel_x, vel_y`). So the whole particle rewrite block (`Exile.cpp:239-360`) is **N/A for sideways** — we just need correct `PS_*` constants pointing at `$2907+N*8` (already done in `Exile.h`).

| Sub-patch | Standard site | Enhanced behavior | Status |
|---|---|---|---|
| `$202b`, `$2032`, `$203d`, `$2045` LDA stack reads | rewrite to `$88xx/$8axx/$89xx/$8bxx` | ROM already reads `$2907,X` etc. | ✅ N/A |
| `$2037/2038/2057/2058: CLC CLC` "always process pixel" | disable offscreen-cull | enhanced has its own visibility check | ⚠️ may want to review |
| `$20e6 → $9300` (particle physics rewrite) | new physics loop | enhanced's physics at `$2120+` natively handles 8-stride | ✅ N/A |
| `$213d → $9400` (particle-copy rewrite) | ditto | ditto | ✅ N/A |
| `$2160: CPX #&7e` (max 127 particles) | raise cap | enhanced already allows more | ✅ N/A |
| `$2168: STX $1e59` (count_x8 store) | relocate | enhanced stores count at `$1E8B`, count×8 at `$1E8C` | ✅ addresses already in `Exile.h` |
| `$224f..$2254` self-mod jump | reroute | enhanced equivalent `$2284` area | ⚠️ verify |

---

## 4. Stars generation (ff50 subroutine)

`$26C8-$26E3` patched so 9× `JSR $FF50` — a fix for HD rendering of big-star areas. Calls `get_random_square_near_player` + determine_background. Enhanced equivalent loop starts at `$26f7` (drift ~`+$2F`).

**Status:** ❌ not ported. Probably not critical for basic playability — stars might just be missing.

---

## 5. Radius / misc HD tweaks

| Patch | Standard | Purpose | Enhanced | Status |
|---|---|---|---|---|
| `$11145: INC $9b` | radius bump | HD wants larger sprite plot radius | enhanced addr drift — probably `$1169` area | ❌ |
| `$0C5A: LDY #&0a` | tweak | HD tile iteration | same region | ⚠️ |
| `$19A7: 0a 0f 0a` | `funny_table_19a7` | table value tweak | same? | ⚠️ |

---

## 6. HD code-correctness fixes

| Patch | Addr | Purpose | Enhanced | Status |
|---|---|---|---|---|
| `$34C6/$34C7 = $18 $18` (BCS → CLC CLC) | `$34C6` std | bypass sprite-too-tall reject when object held | **NOT at $34C6 in enhanced** (that's angle-check) — find sprite-height check elsewhere (search `CMP #&38`) | ❌ critical — applying to enhanced $34C6 would break angle check |

---

## 7. BBC plotter disable

These stop the BBC's native sprite/tile plotting (we render with the C++ PGE sprite engine instead, at HD resolution).

| Patch | Standard | Enhanced | Status |
|---|---|---|---|
| `$0CA5: JMP $0CC0` (object plot off) | `$0CA5` | **same** per `Exile.h` comment; Main.cpp sideways block already writes `0x4C 0xC0 0x0C` at `$0CA5` | ✅ applied |
| `$10D2: JMP $10ED` (tile plot off) | `$10D2` | **same**; Main.cpp sideways already writes it | ✅ applied |

---

## 8. Initialise() corrections

| Patch | Addr | Purpose | Enhanced | Status |
|---|---|---|---|---|
| `$34C6/$34C7 = $18 $18` | see §6 | HD | ❌ different address | ❌ |
| `$492E-$4930 = $82 $A9 $4B` | standard disassembly regression fix | enhanced unaffected | ✅ N/A |

---

## 9. Open research items

- **Why do frames 2+ hang in sideways?** Frame 1 completes in 83k instructions; frame 2 wanders to `$0006`, `$174A`, `$188C`. Stack balance drift? IRQ handler side-effects not mocked? Needs tracing.
- Port §2 properly OR document the 16-object limit visibly to user.
- Locate and port §6 `BCS` fix to enhanced addresses.
- Audit §5 radius / `funny_table` tweaks.

---

## Porting priority (next steps)

1. **Find enhanced equivalent of §6 `BCS → CLC CLC`** — probably in the same `sprites_height_and_vertical_flip_table` region, different address due to code drift.
2. Diagnose §9 frame-2 hang (most likely explanation: something in frame 1 zeros a critical ZP or stack pointer that would normally be re-init by SINIT/IRQ).
3. Only if (1)+(2) don't fix gameplay: tackle §2 full object-stack relocation into a safe main-RAM area.
