# Credits & References

Running list of external code, data, and reference material used in this
fork. Keep in sync as new sources get pulled in so attribution is never
lost.

---

## Upstream project

- **tgd2/exile-olc6502-HD** — original HD renderer project this is forked
  from. All of `olcPixelGameEngine.h`, the initial `olc6502.{h,cpp}` and
  `Bus.{h,cpp}`, the original `Exile.{h,cpp}` / `Main.cpp` structure, and
  the HD object/sprite rendering approach are from tgd2's work.
  Repo: https://github.com/tgd2/exile-olc6502-HD

## Original game

- **Exile** — BBC Micro game by **Peter Irvin** & **Jeremy C. Smith**,
  published by **Superior Software** in 1988. All ROM code (`bmain.rom`,
  `bintro.rom`, `sram.rom`, `srom.rom`, `sinit.rom`, `sinit2.rom`) is the
  original Exile machine code.

## Graphics / game engine

- **olcPixelGameEngine** (`olcPixelGameEngine.h`) by **Javidx9 / OneLoneCoder**
  — 2D game framework providing the window, renderer, input and decal
  primitives used throughout `Main.cpp`.
  Repo: https://github.com/OneLoneCoder/olcPixelGameEngine
  License: OLC-3 / permissive (see header of that file).

## Disassembly work

- **Tom Seddon's `exile_disassembly`** — commented, buildable disassembly
  of standard and enhanced Exile. Used in this fork for:
  - Understanding the game's sound chain (`play_sound` at `$13FA`/`$140F`,
    `push_sound_to_chip` at `$13E4`, the IRQ handler at `$12A6` and its
    sound block at `$1320`, process_sound at `$1399`).
  - Locating sample-trigger PCs (`scream` at `$2497` / `$24CA`,
    hovering-robot sound, clawed-robot teleport).
  - The addresses and meaning of `sound_channels_*` state arrays.
  - Variant address map (standard vs enhanced).
  Repo: https://github.com/tom-seddon/exile_disassembly
  Files referenced locally: `exile-disassembly.txt`,
  `exile-disassembly-2019.txt`, `exile-disassembly-new.txt`,
  `exile-enhanced.txt`.

- **Voice samples** (`samples/[0-6].wav`) — extracted by Tom Seddon from
  the sideways-RAM sample data in `exilesr` (see `samples/samples.py`).
  Files are 8-bit unsigned PCM, mono, 7813 Hz, redistributed in this repo
  alongside `samples.py` and `sounds.py` helper scripts, all from
  `tom-seddon/exile_disassembly/tools/`.

- **BeebAsm** by **stardot** — the assembler used to rebuild the ROM
  files used in this repo. See `SESSION-STATE.md` for the build recipe.
  Repo: https://github.com/stardot/beebasm

## SN76489 chip emulation (`SN76489.{h,cpp}`)

Our SN76489 implementation is modelled directly on jsbeeb's, after
multiple iterations of our own design didn't quite sound right. Specifically:

- **Fractional counter per output sample** (Float64 `counter -= sampleDecrement`,
  reload by adding divider on underflow, toggle `outputBit`) — ported from
  jsbeeb's `_doChannelStep`.
- **Unipolar output** (`outputBit × volume`, summed across channels,
  shifted to bipolar at int16 conversion) — matches jsbeeb's `generate`.
- **Volume table** `pow(10, -0.1*i) / 4`, vol 15 = 0 — jsbeeb's table.
- **Noise channel divisors** 0x10 / 0x20 / 0x40 / tone-ch2-divider —
  jsbeeb's `addFor()`.
- **White-noise LFSR taps** bit 0 XOR bit 1 (BBC's SN76489AN variant,
  NOT the 0 XOR 3 used by the Sega Master System SN76489). LFSR reset
  to `1 << 14` on every noise-channel register write — jsbeeb's `noisePoked`.
- **Tone divider 0 = 1024** (SN76489 spec; jsbeeb follows this).
- **Internal counter tick rate** 250 kHz — matches jsbeeb's
  `waveDecrementPerSecond = soundchipFreq / 2`.

**jsbeeb** by **Matt Godbolt** (and contributors):
- Repo: https://github.com/mattgodbolt/jsbeeb
- File referenced: `src/soundchip.js`
- License: GPL-3.0 (our reimplementation in C++ is a clean-room write
  informed by reading that file; if we ever incorporate jsbeeb code
  verbatim we must comply with GPL-3.0 compatibility).

## System VIA / IC32 addressable-latch behaviour

- **BBC Micro Advanced User Guide** (Bray, Dickens, Holmes — Acorn
  Publishing, 1984) — reference for `$FE40`/`$FE4F`/`$FE43` semantics and
  the IC32 addressable-latch line assignments (line 0 = sound chip /WE).

## macOS audio & I/O

- **Apple CoreAudio / AudioToolbox AudioQueue API** — provides the
  continuous PCM output stream in `BBCSound.cpp` and the per-sample
  playback in `SampleManager.cpp`. Standard Apple framework, no
  third-party code.

- **Apple GLUT framework** (via PGE) — the GLUT window and event loop
  for the app. Deprecated but still functional on macOS.

---

## How to update this file

Whenever new external code, a reference emulator, a disassembly source,
or a helper script is consulted or borrowed from, add an entry above
with:
- Name and author
- Repo / source URL
- License (where known)
- Which file(s) in this repo use it, and how

If code is copied verbatim (even a few lines), note it explicitly so
license compliance can be audited.
