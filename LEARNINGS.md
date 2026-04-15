# Exile-OLC6502-HD: Learnings & Port Notes

Notes from a session debugging tgd2/exile-olc6502-HD on macOS (M5 / arm64 / macOS 26 "Tahoe") and preparing for a TypeScript port.

---

## 1. Build on macOS

The repo's `Makefile` is Linux-only (uses `-lX11 -lGL -lpng -lstdc++fs`). For macOS use `Makefile.mac` (in this repo):

- Uses Apple's `GLUT.framework` + `OpenGL.framework` + `Carbon.framework`.
- libpng from Homebrew: `brew install libpng`.
- Apple's GLUT is deprecated since 10.9, but still works with `-Wno-deprecated-declarations`.
- Sound chip not emulated by the project — no SoLoud needed.
- Build command: `make -f Makefile.mac`. Produces `./exile`.

### macOS window-activation pitfalls

- Running `./exile` directly from terminal works but the window may fail to activate (bouncing dock icon, no window) on macOS 26.
- Wrapping the binary in an `Exile.app` bundle with a minimal `Info.plist` (executable + bundle id) fixes activation.
- After replacing the binary inside the bundle, run `xattr -cr Exile.app && codesign --force --deep --sign - Exile.app` or macOS may refuse to launch with `Code=5 / launchd job spawn failed`.

---

## 2. Disassembly format quirks (level7.org.uk source)

The project parses `exile-disassembly.txt` from `http://www.level7.org.uk/miscellany/exile-disassembly.txt`. The file format has changed since the project was written (~2020); the parser needs three fixes for the current file:

### 2.1. Address-marker prefix and length

- Old format: `#0100: 00 00 …` — first token is length 6 (`#`+4 hex+`:`)
- New format: `&0100 00 00 …` — first token is length 5 (`&`+4 hex, no colon)

The original parser's `bLoadStarted` trigger was `sLine.substr(0,5) == "#0100"` and the address validator required length 6. Both must accept the `&XXXX` form too.

### 2.2. Robust hex parsing

The new file mixes prose with code. A bare `std::stoi` on a non-hex token throws and aborts the program. Wrap stoi in `try/catch` and verify the entire token was consumed (`pos == sWord.length()`).

### 2.3. Self-mod / alternate-version annotations (CRITICAL)

The new disassembly shows certain self-modifying code regions twice — once with the boot-time bytes, once with the runtime-modified bytes — and the second listing has a **lower address than the first**. Example around `$4b90`:

```
&4b90 de b5 07           DEC $07b5,X       ; original
&4b93 20 fa 13           JSR $13fa
&4b96 72 a5 7b 85        ; sound data
&4b91 4c 29 25           JMP $2529          ; ← address goes BACKWARDS;
                                            ;   this is the "after key collected" version
&4b9d a4 3b              LDY $3b
```

A naive last-write-wins parser corrupts `$4b91-93` with the runtime version, so at boot the game executes nonsense. The fix is **first-write-wins for disassembly load**:

```cpp
static std::array<bool,65536> bWritten{};
if (!bWritten[nRam]) { BBC.ram[nRam] = (uint8_t)v; bWritten[nRam] = true; }
nRam++;
```

`PatchExileRAM` itself calls `ParseAssemblyLine` (via `P += "&XXXX: …"`) and **must** be allowed to overwrite. Use a `bFirstWriteWins` member flag set true only inside `LoadExileFromDisassembly`, false elsewhere.

---

## 3. The "minor corrections" patch list is largely obsolete

`LoadExileFromDisassembly` has 37 hardcoded `BBC.ram[…] = …` "corrections" written for the 2020 disassembly. Verified against the current file:

| Status | Count | Meaning |
|---|---|---|
| New file already has the patched value | 24 | redundant |
| New file has a *different* value | 4 | maintainer updated; project's old patch would *corrupt* |
| New file still has the old buggy value | 4 | still needed (genuine bug fixes) |
| New file is missing the byte | 1 | still needed |

**Net: keep only `$34C6=0x18, $34C7=0x18, $492E=0x82, $492F=0xA9, $4930=0x4B`. Drop the other 32.**

---

## 4. Disassembly is not authoritative — use Tom Seddon's source instead

The `level7.org.uk` text disassembly is hand-maintained and **diverges from the real BBC ROM**. We compared every byte of the current text disassembly against Tom Seddon's BeebAsm-buildable disassembly (https://github.com/tom-seddon/exile_disassembly), which assembles into bit-perfect copies of the real `B.MAIN` and `B.INTRO` files:

- 2019 text disassembly: matches Tom's ROM in **99.9 %** of bytes
- Current text disassembly: matches **99.6 %** — i.e. **107 byte regressions** vs the real ROM

These regressions silently break game features (a missing horizontal door on the main screen turned out to be the canary).

### The fix: load Tom's binary directly

Build BeebAsm from source (CMake) and Tom's disassembly:

```bash
git clone https://github.com/stardot/beebasm        # CMake build
git clone https://github.com/tom-seddon/exile_disassembly
# Tom's Makefile uses python2 helpers — patch disk_conv.py for python3:
#   - bytes->list(data) instead of [ord(x) for x in …]
#   - integer divs (//) for /256, /10
#   - bytes() not str.join() for file writes
# Then `make` produces tmp/exileb/0/{BMAIN,BINTRO} and tmp/exilemc/0/{SRAM,SROM,SINIT,SINIT2}
```

Load addresses (from `.lea` files):

| File | Load addr | Size | Variant |
|---|---|---|---|
| `BMAIN` | `$0100` | 24 576 | BBC Micro standard (post-relocation) |
| `BINTRO` | `$7200` | 128 | BBC Micro standard |
| `SRAM` | `$1200` | 15 733 | BBC Master sideways-RAM |
| `SROM` | `$99EC` | 9 748 | BBC Master sideways-RAM (sideways ROM area) |
| `SINIT` | `$7690` | 224 | BBC Master sideways-RAM |
| `SINIT2` | `$6489` | 263 | BBC Master sideways-RAM |

`Exile::LoadExileFromBinary(filename, loadAddr)` is a 5-line method — copy the file into `BBC.ram[loadAddr…]`. With this, you can drop the disassembly parser entirely and skip every disassembly correction.

---

## 5. Initial-render pitfall (macOS only)

The project does heavy work in `OnUserCreate`:
- `LoadExileFromDisassembly` (or `LoadExileFromBinary`)
- `PatchExileRAM`
- `Initialise` → `GenerateSpriteSheet` + `GenerateBackgroundGrid` (the latter runs the 6502 emulator 65 536 times, ~5 s on M5).

PGE's GLUT path on macOS doesn't show the window until `glutMainLoop()` runs, which only happens **after** `OnUserCreate` returns. Result: bouncing/idle dock icon for 5+ seconds with no UI feedback.

Mitigation options (not yet applied to the working build):
- Defer `GenerateBackgroundGrid` to the first `OnUserUpdate` and paint a "Loading…" splash in the very first frame.
- Or pump GLUT events explicitly during init (requires freeglut's `glutMainLoopEvent`, not in Apple GLUT).

---

## 6. Variants now buildable

Two compile-time variants live in this repo, switched by `EXILE_VARIANT_SIDEWAYS_RAM`:

```bash
# Standard (BBC Micro, full HD patches, working)
make -f Makefile.mac                                       # → ./exile (or ./exile-standard saved copy)

# Sideways-RAM (BBC Master, no HD patches, NOT yet rendering — see §7)
clang++ -DEXILE_VARIANT_SIDEWAYS_RAM -c -o Main.o Main.cpp
clang++ -o exile-sideways Bus.o Exile.o olc6502.o Main.o \
        -L$(brew --prefix libpng)/lib -lpng \
        -framework GLUT -framework OpenGL -framework Carbon -pthread -flto
```

`PatchExileRAM` is skipped on the sideways-RAM variant because every patch site uses standard-layout addresses.

---

## 7. Open issues

### 7.1. Sideways-RAM variant: stuck in BRK loop

After `Initialise()` (the 6502 background-grid generation), the sideways binary is alive (`OnUserUpdate` running) but the window never paints. Sample shows the 6502 PC bouncing through `BRK`, the same symptom as zeroed RAM.

Likely causes:
- `Exile.h` constants are hardcoded for the standard layout. Notably:
  - `GAME_RAM_STARTGAMELOOP = 0x19b6` — almost certainly different in the sideways/enhanced version (per the enhanced disassembly `physics engine` block shifted from `$1a0b-$1e18` → `$1a2f-$1e4c`).
  - The `JSR &2398` inject in `GenerateBackgroundGrid` targets a routine at `$2398` in the standard version — the equivalent in the enhanced version is at a different address.
  - Object/particle/tile-grid base addresses (`$0860`, `$08c8`, `$0095`, etc.) likely shift too.
- Without porting these, the emulator runs the bytes that *are* loaded but PC quickly walks off into uninitialised memory (`$0000` = `BRK`).

### 7.2. Fullscreen scaling

The macOS window can be maximised, but PGE keeps the canvas fixed at `SCREEN_WIDTH=1280, SCREEN_HEIGHT=720` with letterboxing — the rest of the screen stays the macOS desktop colour. PGE's GLUT macOS path does not auto-scale the canvas to the host window.

Quick options:
- Bump `SCREEN_WIDTH/HEIGHT` to native panel resolution (e.g. 3024×1964 on 14"/16" MBP), and reduce `SCREEN_ZOOM` so the playfield stays the same physical size — costs more CPU per frame but fills the screen.
- Or set `SCREEN_FULLSCREEN = true` so PGE asks GLUT for fullscreen; this *should* hand back the actual screen dimensions and let PGE scale viewport to fit.
- Long-term: switch off Apple GLUT entirely (deprecated and slow); use SDL2 or a Cocoa-native window with a Metal/OpenGL viewport. The PGE community has SDL2 forks.

---

## 8. Notes for the TypeScript port

When porting to TypeScript (likely WebAssembly + WebGL or a Canvas2D first cut):

1. **Authoritative ROM bytes.** Bundle Tom Seddon's `BMAIN`/`BINTRO` as a binary asset (~24 KB total). Don't try to re-parse the level7 text disassembly — it's hand-maintained and drifts from real ROM.
2. **Memory map.** BBC RAM is a flat 64 KB `Uint8Array`. Load `BMAIN` at offset `0x0100`, `BINTRO` at `0x7200`. Sideways-RAM variant if needed: SRAM/SROM/SINIT/SINIT2 per §4.
3. **HD patches.** The big `P += "…"` text-assembly blob in `Exile::PatchExileRAM` should become a typed table:
   ```ts
   type Patch = { addr: number; bytes: number[]; note: string };
   const HD_PATCHES: Patch[] = [
     { addr: 0x34C6, bytes: [0x18, 0x18], note: 'CLC CLC for HD render' },
     { addr: 0x1a62, bytes: [0x4c, 0x10, 0xff], note: 'JMP to $ff10 hook' },
     // …
   ];
   ```
   It will be much easier to audit and to vary by variant (don't apply on sideways-RAM).
4. **CopyRAM**. The "object stack relocation" copies live data from `$0860+`, `$08c8+`, etc. to `$9600+`, `$9700+`, …. This must run **after** the ROM bytes are loaded (state is initial-game state at boot in the BMAIN image).
5. **Game loop entry.** The standard layout's "start of game loop" PC is `$19B6`. The HD project's outer loop runs the 6502 until PC returns to that address (one game frame). Verify this is still right after every patch round.
6. **Background grid generation** is a hot loop: 65 536 invocations of the BBC `$2398` routine to classify every tile. Plan for either (a) running it once at load with a splash screen, (b) caching the result to disk/IndexedDB, or (c) running it incrementally over multiple frames so the UI is never blocked.
7. **Self-mod / alternate-listing immunity.** If you ever fall back to text-disassembly loading for any region, use first-write-wins for the load pass and last-write-wins for the patch pass — see §2.3.
8. **Deviations from the real game** to be aware of:
   - SN76489 sound chip is not emulated.
   - End-game ship animation is broken because the BBC scrolling is bypassed.
   - Underwater colour is a transparent overlay rather than the BBC's XOR sprite trick.
   - Object/particle stacks expanded from 16/64 to 128/127 to keep the wider HD screen busy (see "Increasing primary stack to 128 objects" patch group).
9. **Cheats/debug.** Keys 1–4 in HD trigger: 1 = give all equipment, 2 = pocket any item, 3 = debug grid, 4 = debug overlay. The first two write directly into the inventory bytes; useful as smoke tests in the port.

---

## 9. Verified working state in this repo

- `exile` / `exile-standard`: BBC Micro standard, loads `bmain.rom` + `bintro.rom`, applies HD patches, renders correctly with horizontal door visible.
- `exile-sideways`: BBC Master sideways-RAM, loads `sram.rom`/`srom.rom`/`sinit.rom`/`sinit2.rom`, no HD patches. Builds and boots far enough to generate the background grid, but does not render — needs §7.1 work.
- `Exile.app`: macOS bundle wrapping `exile` for proper window activation.
- `Makefile.mac`: macOS build using GLUT/OpenGL/Carbon frameworks + Homebrew libpng.
- Disassembly parser fixes (`&XXXX` form, try/catch around `stoi`, first-write-wins) live in `Exile.cpp::ParseAssemblyLine` even though the binary loader path doesn't use them — they're correct fixes if anyone wants to run from the level7 text file.

---

## 10. Useful commands

```bash
# Diff the loaded RAM against the real ROM (sanity check):
python3 - <<'PY'
import re
def parse(p):
    d={}
    for L in open(p,errors='ignore'):
        m=re.match(r'^[#&]([0-9a-f]{4}):?\s+(.*)$', L.rstrip())
        if not m: continue
        a=int(m.group(1),16)
        for t in m.group(2).split(';')[0].split('#')[0].split():
            if re.fullmatch(r'[0-9a-f]{2}',t):
                if a not in d: d[a]=int(t,16)
                a+=1
            elif t=='--': break
            else: break
    return d
bm=open('bmain.rom','rb').read()
new=parse('exile-disassembly.txt')
miss=[a for a in range(0x100, 0x100+len(bm)) if new.get(a)!=bm[a-0x100]]
print(f'{len(miss)} bytes differ between text disassembly and real ROM')
PY

# Count diff regions between any two text disassemblies:
diff <(grep -E '^[#&]' a.txt) <(grep -E '^[#&]' b.txt) | wc -l
```
