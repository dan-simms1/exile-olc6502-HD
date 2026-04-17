#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"

#include "Exile.h"
#include "SampleManager.h"
#include "BBCSound.h"
#include <chrono>
#include <cmath>
#include <cctype>
#include <thread>
#ifndef BISECT_LO
#define BISECT_LO 0
#endif
#ifndef BISECT_HI
#define BISECT_HI 0
#endif
#ifndef BISECT_HI
#define BISECT_HI 0xFFFF
#endif

// O------------------------------------------------------------------------------O
// | Screen constants and global variables                                        |
// O------------------------------------------------------------------------------O
uint32_t nFrameCounter = 0;
float fGlobalTime = 0;
float fTimeSinceLastFrame = 0;
std::chrono::duration<double> Time_GameLoop;
std::chrono::duration<double> Time_DrawScreen;

const float SCREEN_WIDTH = 1280;        const float SCREEN_HEIGHT = 720; // 720p display
// Default windowed — macOS's GLUT fullscreen APIs (glutFullScreen / glutReshapeWindow) crash
// with NSTrackingRectTag errors on Tahoe. Use the native green button or a supported WM shortcut
// to maximise; our reshape handler (in olcPixelGameEngine.h) scales the canvas to fit.
#ifndef SCREEN_FULLSCREEN
const bool SCREEN_FULLSCREEN = false;
#endif
const bool SCREEN_VSYNC = true;
const float SCREEN_ZOOM = 2.0f;
const float SCREEN_BORDER_SCALE = 0.3f; // To trigger scrolling

char gMode = 'C';  // 'A' = BBC-native framebuffer, 'C' = HD renderer (default)

float fCanvasX = 4350;
float fCanvasY = 1570;

float fCanvasWidth = SCREEN_WIDTH / SCREEN_ZOOM;
float fCanvasHeight = SCREEN_HEIGHT / SCREEN_ZOOM;
float fCanvasOffsetX = (SCREEN_WIDTH - fCanvasWidth) / 2.0f;
float fCanvasOffsetY = (SCREEN_HEIGHT - fCanvasHeight) / 2.0f;

float fScrollShiftX = 0; // For smooth scrolling
float fScrollShiftY = 0;

std::unique_ptr<olc::Sprite> sprWater[2];        std::unique_ptr<olc::Decal> decWater[2];
std::unique_ptr<olc::Sprite> sprWaterSquare[2];  std::unique_ptr<olc::Decal> decWaterSquare[2];

uint16_t sw_lastPc = 0;          // diagnostic: PC at end of the last emulated frame (sideways only)
uint16_t sw_lastPcBefore0 = 0;   // diagnostic: first PC that preceded PC=0 — where it goes off the rails

bool bScreenFlash = false;
uint8_t nEarthQuakeOffset = 0;

olc::Key Keys[39] = { olc::Key::D /* D = Dummy Key */, olc::ESCAPE, olc::F1, olc::F2, olc::F3, olc::F4, olc::F5, olc::F6, olc::F7, olc::F8, olc::Key::D, olc::Key::D,
			  olc::Key::G, olc::SPACE, olc::Key::I, olc::Key::D, olc::Key::D, olc::Key::D, olc::Key::D, olc::Key::K, olc::Key::O, olc::Key::OEM_4 /* '[' */,
			  olc::CTRL, olc::TAB, olc::Key::Y, olc::Key::U, olc::Key::T, olc::Key::R, olc::PERIOD, olc::Key::M, olc::Key::COMMA,
			  olc::Key::S, olc::Key::V, olc::Key::Q, olc::Key::W, olc::Key::P, olc::Key::P, olc::Key::L, olc::SHIFT };

// For debugging overlay:
bool bShowDebugGrid = false;
bool bShowDebugOverlay = false;
int nObjectCountMax = 0;
int nParticleCountMax = 0;

auto hex = [](uint32_t n, uint8_t d)
{
	std::string s(d, '0');
	for (int i = d - 1; i >= 0; i--, n >>= 4)
		s[i] = "0123456789ABCDEF"[n & 0xF];
	return s;
};
// O------------------------------------------------------------------------------O

// O------------------------------------------------------------------------------O
// | Main Exile class                                                             |
// O------------------------------------------------------------------------------O
class Exile_olc6502_HD : public olc::PixelGameEngine
{

public:
	Exile_olc6502_HD() { sAppName = "Exile"; }

	Exile Game;
	SampleManager Samples;
	BBCSound      Sound;

	// Mode A only: the BBC's own $4000-$7FFF framebuffer decoded into a 128×256 sprite
	// and blit to the window each frame. See DrawBBCFramebuffer().
	std::unique_ptr<olc::Sprite> sprBBCScreen;
	std::unique_ptr<olc::Decal>  decBBCScreen;

	float ScreenCoordinateX(float GameCoordinateX) {
		float x = GameCoordinateX - fCanvasX - fCanvasOffsetX - (fScrollShiftX * fTimeSinceLastFrame / 0.025f);
		x += nEarthQuakeOffset * 4; // Shift screen in event of earthquake
		x = x * SCREEN_ZOOM;
		return x;
	}

	float ScreenCoordinateY(float GameCoordinateY) {
		float y = GameCoordinateY - fCanvasY - fCanvasOffsetY - (fScrollShiftY * fTimeSinceLastFrame / 0.025f);
		y = y * SCREEN_ZOOM;
		return y;
	}

	// Called when CPU PC == play_sound entry. Reads the 4-byte envelope block
	// from the caller's code (immediately after the JSR), then synthesizes
	// a short square-wave (or noise) tone via SampleManager.PlayTone.
	//
	// Game's 4-byte sound parameter format (per disassembly):
	//   byte 0: volume envelope offset (1-127). High bit (>= 0x80) → noise on chan 4.
	//   byte 1: top nibble = initial volume (0-15), bottom = envelope duration
	//   byte 2: frequency envelope offset
	//   byte 3: top nibble = initial pitch (0-15), bottom = envelope duration
	void DispatchPlaySound() {
		// Stack layout right after JSR: top of stack holds (return_addr-1)
		// in two bytes: low at $100+(SP+1), high at $100+(SP+2).
		uint8_t sp  = Game.BBC.cpu.stkp;
		uint8_t lo  = Game.BBC.ram[0x0100 + ((sp + 1) & 0xFF)];
		uint8_t hi  = Game.BBC.ram[0x0100 + ((sp + 2) & 0xFF)];
		uint16_t ra = (uint16_t)lo | ((uint16_t)hi << 8);   // = call_addr - 1
		// 4 envelope bytes are immediately after the JSR (ra+1..ra+4).
		uint8_t b0 = Game.BBC.ram[(uint16_t)(ra + 1)];
		uint8_t b1 = Game.BBC.ram[(uint16_t)(ra + 2)];
		uint8_t b2 = Game.BBC.ram[(uint16_t)(ra + 3)];
		uint8_t b3 = Game.BBC.ram[(uint16_t)(ra + 4)];

		uint8_t volTop  = (b1 >> 4) & 0x0F;
		uint8_t volDur  = (b1     ) & 0x0F;
		uint8_t pitchTop= (b3 >> 4) & 0x0F;
		uint8_t pitchDur= (b3     ) & 0x0F;

		double amp = (double)volTop / 15.0;
		if (amp <= 0.0) amp = 0.4;       // many envelopes start at 0 vol then ramp
		// Noise / shot-style: envelope offset top bit set, or freq envelope == 0
		bool isNoise = (b0 & 0x80) || (b2 == 0xFF);
		double freqHz;
		if (isNoise) {
			freqHz = -1.0;               // PlayTone: <=0 → noise
		} else {
			// Map 4-bit initial pitch to ~110Hz..~3500Hz exponentially.
			freqHz = 110.0 * std::pow(2.0, (double)pitchTop * 0.31);
		}
		// Duration: combine the two envelope-duration nibbles.
		int durMs = 30 + (int)(volDur + pitchDur) * 12;
		if (durMs > 400) durMs = 400;

		Samples.PlayTone(freqHz, amp, durMs);
	}

	// Decode Exile's framebuffer (16 KB, $4000-$7FFF) into sprBBCScreen and blit to the window.
	// Used only in Mode A — the BBC's own plotter writes here naturally when HD patches are skipped.
	//
	// Exile uses a CUSTOM 16 KB screen mode: R1 = 64 chars/line (not Mode 2's 80) placed at
	// &4000-&7FFF. This is visible in the disassembly: wipe_screen_loop ($01D5) clears
	// &4000-&7FFF, then "LDA #&01 ; R1" / "LDX #&40" writes 64 to CRTC R1. The Video ULA is
	// still configured for Mode 2 bit-depth (4 bpp, 2 px/byte) — see $69E7 "Use MODE 2".
	//     64 chars × 2 px = 128 px wide;   32 char rows × 8 scanlines = 256 px tall.
	//     64 × 32 × 8 = 16384 bytes = 0x4000 — exactly &4000-&7FFF, and the 16 KB freed at
	//     &3000-&3FFF holds relocated game code (Exile famously packs every byte of RAM).
	//
	// Pixel layout (cross-checked against jsbeeb src/video.js lines 399-421):
	//   Pixel 0 (left)  colour = byte bits 7,5,3,1 → colour bits 3,2,1,0 respectively.
	//   Pixel 1 (right) colour = byte bits 6,4,2,0 → colour bits 3,2,1,0 respectively.
	//
	// Palette: the game writes $FE21 = (logicalXOR0xF << 4) | physicalNibble, where the
	// physicalNibble's low 3 bits are *inverted* (0 = on). After XOR with 0x07, bits encode
	// B,G,R at positions 0,1,2 (verified on real BBC: the "red" palette byte $E6 → phy=6
	// XOR 7 = 1 and jsbeeb's collook[1] = 0xff0000ff in ABGR = pure RED, so bit 0 *is* Blue
	// in the index word and bit 2 is Red).
	//
	// CRTC R12/R13 give a 14-bit MA (memory-address) value; the byte address of top-left is
	// MA*8. For Exile's &4000 screen, MA = 0x0800. Scrolling shifts MA; the 16 KB screen
	// wraps as a circular buffer.
	void DrawBBCFramebuffer()
	{
		// Standard BBC Exile uses an 8 KB screen ring at $6000-$7FFF (what bmain.rom's
		// $01D0 wipes). $4000-$5FFF is off-screen code/data — including it in the decode
		// makes CRTC scroll wrap through it, showing raw bmain.rom bytes as a rainbow
		// middle-band of garbage. R1=64 chars × R9+1=8 scanlines × R6=16 rows = 8192 = 8 KB.
		const int  SCREEN_BASE      = 0x6000;
		const int  SCREEN_SIZE      = 0x2000;   // 8 KB ring
		const int  CHARS_PER_ROW    = 64;
		const int  CHAR_ROWS        = 16;
		const int  PIXELS_WIDE      = CHARS_PER_ROW * 2;   // 128
		const int  PIXELS_TALL      = CHAR_ROWS * 8;       // 128

		uint16_t crtcMA    = ((uint16_t)Game.BBC.crtcR12 << 8) | Game.BBC.crtcR13; // 14-bit
		uint16_t startByte = (crtcMA * 8) & 0x7FFF;        // byte address of top-left
		// Express as offset within the 16 KB circular screen buffer at [$4000, $8000).
		int screenOffset = (startByte - SCREEN_BASE) & (SCREEN_SIZE - 1);

		for (int charRow = 0; charRow < CHAR_ROWS; charRow++) {
			for (int charCol = 0; charCol < CHARS_PER_ROW; charCol++) {
				for (int scan = 0; scan < 8; scan++) {
					int linear   = (charRow * CHARS_PER_ROW + charCol) * 8 + scan;
					int scrolled = (linear + screenOffset) & (SCREEN_SIZE - 1);
					uint8_t b    = Game.BBC.ram[SCREEN_BASE + scrolled];
					int py = charRow * 8 + scan;
					for (int n = 0; n < 2; n++) {
						int logCol = (((b >> (7 - n)) & 1) << 3)
						           | (((b >> (5 - n)) & 1) << 2)
						           | (((b >> (3 - n)) & 1) << 1)
						           |  ((b >> (1 - n)) & 1);
						// BBC physical colour: lower nibble of $FE21 write is active-high direct
						// (not inverted — VDU 19 arg 0=black, 1=red, 2=green, 3=yellow, 4=blue,
						// 5=magenta, 6=cyan, 7=white). Bit 0 = Red, bit 1 = Green, bit 2 = Blue.
						uint8_t phy = Game.BBC.videoULAPalette[logCol];
						uint8_t r  = (phy & 1) ? 0xFF : 0;
						uint8_t g  = (phy & 2) ? 0xFF : 0;
						uint8_t bv = (phy & 4) ? 0xFF : 0;
						sprBBCScreen->SetPixel(charCol * 2 + n, py, olc::Pixel(r, g, bv));
					}
				}
			}
		}
		decBBCScreen->Update();
		// Stretch 128×256 → 1280×720 (10× horizontal, ~2.81× vertical). Matches Mode C pixel
		// aspect (the HD renderer's view), not the BBC CRT 1:1 ratio.
		DrawDecal({0, 0}, decBBCScreen.get(),
		          { SCREEN_WIDTH / (float)PIXELS_WIDE, SCREEN_HEIGHT / (float)PIXELS_TALL });
	}

	bool OnUserCreate()
	{
		Clear(olc::BLACK);

		// Setup water sprites:
		for (int i = 0; i < 2; i++) {
			olc::Pixel nCol;
			if (i == 0) nCol = olc::Pixel(0, 0, 0xff); // Blue for background
			else nCol = olc::Pixel(0, 0, 0xff, 0x60); // Transparent blue for foreground

			sprWater[i] = std::make_unique<olc::Sprite>(GAME_TILE_WIDTH, SCREEN_HEIGHT + GAME_TILE_HEIGHT);
			olc::PixelGameEngine::SetDrawTarget(sprWater[i].get());
			olc::PixelGameEngine::Clear(nCol);
			if (i == 0) olc::PixelGameEngine::DrawLine(0, 0, GAME_TILE_WIDTH, 0, olc::CYAN);
			decWater[i] = std::make_unique<olc::Decal>(sprWater[i].get());

			sprWaterSquare[i] = std::make_unique<olc::Sprite>(GAME_TILE_WIDTH, GAME_TILE_HEIGHT);
			olc::PixelGameEngine::SetDrawTarget(sprWaterSquare[i].get());
			olc::PixelGameEngine::Clear(nCol);
			decWaterSquare[i] = std::make_unique<olc::Decal>(sprWaterSquare[i].get());
		}
		int nNull = 0;
		olc::PixelGameEngine::SetDrawTarget(nNull);

		// Load Exile RAM from authoritative ROM (built from Tom Seddon's BeebAsm disassembly).
		// Choose at compile time between BBC Micro standard and sideways-RAM (BBC Master) variants.
#ifdef EXILE_VARIANT_SIDEWAYS_RAM
		// Sideways-RAM (BBC Master) variant: load clean BeebAsm-built binaries.
		//   SRAM is assembled to run at $0100 (the .lea load=$1200 is the BBC's pre-relocation address).
		//   SROM is sideways-ROM content; on real hardware it pages into $8000-$BFFF. Enable the
		//   Bus's paged-ROM emulation (ROMSEL $FE30) and load SROM into sideways bank 0, at the
		//   offset the game expects ($99EC → $8000 + $19EC within the bank).
		//   SINIT/SINIT2 are boot routines; we pre-place memory directly so they aren't executed.
		Game.BBC.bSidewaysPaging = true;
		Game.BBC.activeBank = 0;
		Game.LoadExileFromBinary("sram.rom",   0x0100);   // main code/data
		Game.LoadExileFromBinary("sinit.rom",  0x7690);   // boot init (loaded, not executed — same as how standard loads bintro.rom but never runs it)
		Game.LoadExileFromBinary("sinit2.rom", 0x6489);   // boot init 2 (data bytes at $6489-$6494 + code at $6495)
		Game.LoadExileFromBinaryToBank("srom.rom", /*bank=*/0, /*offsetInBank=*/0x99EC - 0x8000);

		// DO NOT zero $0100-$01FF. Although it overlaps the 6502 stack page, sram.rom
		// loads REAL CODE + DATA here: $01A8 process_actions is called from sound code
		// in sideways bank 0 ($A6D3), and $01D0 wipe_screen_and_start_game is the end
		// of SINIT2. Zeroing them replaces working routines with BRK cascades and sends
		// the CPU walking through zero bytes until it hits SINIT2's decrypt loop.
		// The stack grows down from $01FF; on real hardware the game keeps its stack
		// shallow so it never reaches down to the $01A8 code region.

		// Apply all HD patches + IRQ-vector fakes for the enhanced ROM.
		Game.PatchEnhancedExileRAM();
#else
		// BBC Micro standard layout: BMAIN $0100-$60FF (post-relocation), BINTRO at $7200.
		Game.LoadExileFromBinary("bmain.rom",  0x0100);
		Game.LoadExileFromBinary("bintro.rom", 0x7200);
		if (gMode != 'A') {
			// Modes B/C: apply HD patches. These (a) copy object/particle stacks to
			// $9600+ (required because olc6502::ReloactedStackAddress hard-coded
			// redirect reads from there), (b) rewrite sprite/tile operands to the
			// new locations, (c) overwrite $0CA5/$10D2/$34C6 to JMP past the BBC
			// native plotter so the C++ HD renderer draws from extracted game state.
			Game.PatchExileRAM();
		} else {
			// Mode A: the original Exile ROM. Everything HD does is *added* by
			// PatchExileRAM; for native BBC rendering we must skip it entirely.
			// That also means disabling the CPU's zero-page redirect (otherwise
			// every object-stack read returns zero from never-written $9600+ RAM).
			//
			// Critically — do NOT wipe $4000-$7FFF. That region LOOKS like boot-
			// only leftovers but actually contains permanent game data: $4FEC+
			// holds the map_data (disassembly line 4930: "ADC #&af ; &4fec =
			// map_data") that the plotter reads via $A4/$A5 pointer. On a real
			// BBC this data is visible as noise at first, then the game's own
			// tile plotter overwrites it with tile graphics on startup. Wiping
			// it kills the map; the game then walks into what's now a BRK at
			// an empty map row and the CPU lands at PC=$0.
			Game.BBC.cpu.bDisableStackRelocation = true;
		}
		// NOTE: do NOT wipe $4000-$7FFF here. bmain.rom places executable game code at
		// $4000-$60FF and the HD build (which this Mode A build still shares) calls into
		// that range. The BBC intro would wipe this memory to set up screen display, but
		// we bypass the intro — zeroing it now would turn those code bytes into BRKs and
		// crash the game to PC=$0. Mode A's render just shows whatever the HD game leaves
		// in the "screen area" (mostly particles on code-byte noise).
#endif
		Game.Initialise(gMode == 'A');  // Mode A skips the ~5s HD sprite-sheet + grid-gen pre-pass

		// Mode A: allocate the 128×256 sprite/decal pair we blit the BBC framebuffer into each
		// frame. Exile uses a custom 16 KB mode — R1=64 chars/line, screen at &4000-&7FFF —
		// giving 128×256 logical pixels at Mode 2 (4 bpp) bit-depth. See DrawBBCFramebuffer.
		if (gMode == 'A') {
			sprBBCScreen = std::make_unique<olc::Sprite>(128, 128);
			decBBCScreen = std::make_unique<olc::Decal>(sprBBCScreen.get());
		}

		// Load Tom Seddon's voice samples (Master enhanced). Missing/absent samples
		// are non-fatal; we just won't play them.
		Samples.LoadDirectory("samples");

		// Wire SN76489 emulator: Bus → BBCSound on System VIA writes.
		Game.BBC.sound = &Sound;
		Sound.Start();

		return true;
	}

	bool OnUserUpdate(float fElapsedTime)
	{
		// (Sideways-RAM stub removed — the Bus now emulates ROMSEL/$FE30 bank switching, so
		//  the real game loop below should run; if it still drops to PC=0 we need to dig
		//  further into boot state setup.)
		// Process cheat and debug keys:
		if (!GetKey(olc::CTRL).bHeld) {
			if (GetKey(olc::K1).bPressed) Game.Cheat_GetAllEquipment();
			if (GetKey(olc::K2).bPressed) Game.Cheat_StoreAnyObject();
			if (GetKey(olc::K3).bPressed) bShowDebugGrid = !bShowDebugGrid;
			if (GetKey(olc::K4).bPressed) bShowDebugOverlay = !bShowDebugOverlay;
		}
		// Sample debug keys — CTRL + 0..6 plays samples 0..6.
		// (Mac F-keys reserved for media controls by default; numpad not
		//  on most MacBooks. SHIFT is a game input key. CTRL is already
		//  used as a debug modifier (CTRL+arrows = move through walls),
		//  so CTRL+digit fits the "CTRL = debug" convention.)
		if (GetKey(olc::CTRL).bHeld) {
			if (GetKey(olc::K0).bPressed) Samples.Play(0);  // "Welcome to the land of the exile"
			if (GetKey(olc::K1).bPressed) Samples.Play(1);  // "Ow!"
			if (GetKey(olc::K2).bPressed) Samples.Play(2);  // "Ow"
			if (GetKey(olc::K3).bPressed) Samples.Play(3);  // "Ooh"
			if (GetKey(olc::K4).bPressed) Samples.Play(4);  // "Oooh!"
			if (GetKey(olc::K5).bPressed) Samples.Play(5);  // "Destroy!"
			if (GetKey(olc::K6).bPressed) Samples.Play(6);  // "Radio die"
		}

		// (Runtime fullscreen toggle removed — Apple GLUT's glutFullScreen / glutReshapeWindow
		//  crashes with NSInternalInconsistencyException on Tahoe. Use the macOS green button
		//  to maximise; the reshape handler in PGE rescales the canvas to fit.)

		fGlobalTime = +fElapsedTime;
		nFrameCounter = (nFrameCounter + 1) % 0xFFFF;

		// BBC sound IRQ scheduler — fires the fake interrupt on real
		// wall-clock time so envelope/duration ticks match a real BBC
		// regardless of PGE/game-loop rate.
		//
		// CRITICAL: real BBC fires TWO IRQs per VSync (timer-1 for the
		// water-level palette swap, then VSync itself for frame end), but
		// ONLY the VSync one runs the sound code. Handler at $12A6 does
		// `BVC $12C8` — if V flag from $FE4D bit 6 is set (timer 1), it
		// detours to the palette swap at $12B6 and EXITS EARLY via $12C5
		// → $1392 leave_interrupt, never reaching the sound block at $1320.
		// Only when V=0 does execution fall through to the sound update.
		// So the actual sound-update rate on real hardware is ~50 Hz
		// (one per VSync), NOT 100 Hz.
		//
		// We fire at 50 Hz with $FE4D := 0x80 (bit 7 set, bit 6 clear) so
		// BPL fails and BVC takes — exactly the "sound path" of the real
		// IRQ. A is stashed in $FC for leave_interrupt (LDA $FC before
		// RTI). I flag is cleared so olc6502::irq() fires.
		{
			static double sIrqAccumSec = 0.0;
			constexpr double kBbcSoundIrqPeriodSec = 0.02;  // 50 Hz (sound IRQ only runs on VSync path)
			sIrqAccumSec += (double)fElapsedTime;
			int nMaxBatch = 5;  // cap if we ever fall hugely behind
			while (sIrqAccumSec >= kBbcSoundIrqPeriodSec && nMaxBatch-- > 0) {
				sIrqAccumSec -= kBbcSoundIrqPeriodSec;
				uint16_t pcSave = Game.BBC.cpu.pc;
				Game.BBC.ram[0x00FC] = Game.BBC.cpu.a;
				Game.BBC.ram[0xFE4D] = 0x80;
				Game.BBC.cpu.status &= ~olc6502::I;
				Game.BBC.cpu.irq();
				int nIrqCap = 200000;
				do {
					do Game.BBC.cpu.clock();
					while (!Game.BBC.cpu.complete());
					if (--nIrqCap <= 0) break;
				} while (Game.BBC.cpu.pc != pcSave);
			}
			if (sIrqAccumSec > 0.1) sIrqAccumSec = 0.0;  // hard reset on long stall
		}

		// O------------------------------------------------------------------------------O
		// | Process keys - Part 1 (capture key presses with every PGE frame)             |
		// O------------------------------------------------------------------------------O
		for (int nKey = 0; nKey < 39; nKey++) {
			if (GetKey(Keys[nKey]).bPressed || GetKey(Keys[nKey]).bHeld) {
				if (Keys[nKey] != olc::D) { // Ignore D (dummy) key
					Game.BBC.ram[GAME_RAM_INPUTS + nKey] = Game.BBC.ram[GAME_RAM_INPUTS + nKey] | 0x80;
				}
			}
		}
		// O------------------------------------------------------------------------------O


		// O++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++O
		// O++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++O
		// + PROCESS GAME LOOP EVERY 0.025ms                                              +
		// O++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++O
		// O++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++O
		if ((fTimeSinceLastFrame += fElapsedTime) > 0.025f) {
			fTimeSinceLastFrame = 0.0f;

			// For debugging - time game loop:
			auto TimeStart_GameLoop = std::chrono::high_resolution_clock::now();

			// For debugging - CTRL + CURSOR MOVES PLAYER THROUGH WALLS:
			if (GetKey(olc::CTRL).bHeld) {
				if (GetKey(olc::LEFT).bPressed || GetKey(olc::LEFT).bHeld)   Game.BBC.ram[OS_X]--;
				if (GetKey(olc::RIGHT).bPressed || GetKey(olc::RIGHT).bHeld) Game.BBC.ram[OS_X]++;
				if (GetKey(olc::UP).bPressed || GetKey(olc::UP).bHeld)       Game.BBC.ram[OS_Y]--;
				if (GetKey(olc::DOWN).bPressed || GetKey(olc::DOWN).bHeld)   Game.BBC.ram[OS_Y]++;
			}

			// O------------------------------------------------------------------------------O
			// | Run BBC game loop                                                            |
			// O------------------------------------------------------------------------------O
			bScreenFlash = false;

			Game.BBC.cpu.pc = GAME_RAM_STARTGAMELOOP;
			// Welcome sample fires once after a brief settle so initialisation
			// audio doesn't compete. Counter survives across game-loop iterations.
			static int nWelcomeFrameCountdown = 10;  // ~0.25s at 40 game ticks/s
			if (nWelcomeFrameCountdown > 0 && --nWelcomeFrameCountdown == 0) {
				Samples.Play(0);  // "Welcome to the land of the exile"
			}

#ifdef EXILE_VARIANT_SIDEWAYS_RAM
			// Frames now complete naturally back to $19DA (vsync trap at $1F99 in olc6502
			// lets the wait_for_vsync loop exit). A generous cap (1M) is kept purely as a
			// safety net in case a frame ever hangs — normal frames take well under 100k instr.
			int nCycleSafetyCap = 1000000;
			do {
				do Game.BBC.cpu.clock();
				while (!Game.BBC.cpu.complete());
				if (Game.BBC.cpu.pc == GAME_RAM_SCREENFLASH) bScreenFlash = (Game.BBC.cpu.a == 0);
				if (Game.BBC.cpu.pc == GAME_RAM_EARTHQUAKE) nEarthQuakeOffset = (Game.BBC.cpu.a & 1);
				// Sample traps: play the WAV AND jump past the rest of the 6502
				// routine so its own play_sound / play_sample calls don't
				// double up (the BBC scream sound on top of "Ow!" was audible
				// and may have been contributing to the periodic thud).
				// Mode A skips these — pure BBC experience uses only SN76489 chip sound.
				if (gMode != 'A') {
					if (Game.BBC.cpu.pc == SAMPLE_TRAP_SCREAM) {
						Samples.Play(1 + (rand() & 3));
						Game.BBC.cpu.pc = SAMPLE_TRAP_SCREAM_SKIP_TO;
					}
					if (Game.BBC.cpu.pc == SAMPLE_TRAP_HOVERING_ROBOT) {
						Samples.Play(6);
						Game.BBC.cpu.pc = SAMPLE_TRAP_HR_SKIP_TO;
					}
					if (Game.BBC.cpu.pc == SAMPLE_TRAP_CLAWED_ROBOT) {
						Samples.Play(5);
						Game.BBC.cpu.pc = SAMPLE_TRAP_CR_SKIP_TO;
					}
				}
				if (--nCycleSafetyCap <= 0) break;
			} while (Game.BBC.cpu.pc != GAME_RAM_STARTGAMELOOP);
#else
			// Safety cap: under Mode C the HD patches keep main_game_loop fast, but Mode A
			// (unpatched game) can wander off into code we don't support (OSBYTE, stuck
			// wait loops) — without this cap the C++ loop hangs forever. One million
			// 6502 instructions is far more than a healthy frame needs (~50k).
			int nCycleSafetyCapStd = 1'000'000;
			uint16_t lastValidPC = Game.BBC.cpu.pc;
			uint16_t prevValidPC = lastValidPC;
			do {
				if (Game.BBC.cpu.pc >= 0x0100 && Game.BBC.cpu.pc < 0x8000) {
					prevValidPC = lastValidPC;
					lastValidPC = Game.BBC.cpu.pc;
				}
				do Game.BBC.cpu.clock();
				while (!Game.BBC.cpu.complete());
				if (Game.BBC.cpu.pc == GAME_RAM_SCREENFLASH) bScreenFlash = (Game.BBC.cpu.a == 0);
				if (Game.BBC.cpu.pc == GAME_RAM_EARTHQUAKE) nEarthQuakeOffset = (Game.BBC.cpu.a & 1);
				// Mode A skips voice sample traps — pure BBC uses only SN76489 chip sound.
				if (gMode != 'A') {
					if (Game.BBC.cpu.pc == SAMPLE_TRAP_SCREAM) {
						Samples.Play(1 + (rand() & 3));
						Game.BBC.cpu.pc = SAMPLE_TRAP_SCREAM_SKIP_TO;
					}
					if (Game.BBC.cpu.pc == SAMPLE_TRAP_HOVERING_ROBOT) {
						Samples.Play(6);
						Game.BBC.cpu.pc = SAMPLE_TRAP_HR_SKIP_TO;
					}
					if (Game.BBC.cpu.pc == SAMPLE_TRAP_CLAWED_ROBOT) {
						Samples.Play(5);
						Game.BBC.cpu.pc = SAMPLE_TRAP_CR_SKIP_TO;
					}
				}
				if (--nCycleSafetyCapStd <= 0) {
					static int nStallWarn = 0;
					if (nStallWarn++ < 5) {
						std::cout << "game-loop stall, pc=$" << std::hex
						          << Game.BBC.cpu.pc << " lastValidPC=$" << lastValidPC
						          << " prevValidPC=$" << prevValidPC
						          << std::dec << std::endl;
					}
					break;
				}
			} while (Game.BBC.cpu.pc != GAME_RAM_STARTGAMELOOP);
#endif

			// Fake IRQ scheduling moved out of this block — see top of
			// OnUserUpdate so it accumulates true elapsed time, not just
			// when the 25 ms game tick happens.
			// O------------------------------------------------------------------------------O

			// O------------------------------------------------------------------------------O
			// | Process keys - Part 2 (mark "held", or reset)                                |
			// O------------------------------------------------------------------------------O
			for (int nKey = 0; nKey < 39; nKey++) {
				if (GetKey(Keys[nKey]).bHeld) {
					if (Keys[nKey] != olc::D) { // Ignore D (dummy) key
						Game.BBC.ram[GAME_RAM_INPUTS + nKey] = 0x40;
					}
				}
				else Game.BBC.ram[GAME_RAM_INPUTS + nKey] = 0x00;
			}
			// O------------------------------------------------------------------------------O

			// O------------------------------------------------------------------------------O
			// | Process scrolling                                                            |
			// O------------------------------------------------------------------------------O
			fCanvasX += fScrollShiftX;
			fCanvasY += fScrollShiftY;
			fScrollShiftX = 0;
			fScrollShiftY = 0;

			Obj O = Game.Object(0); // Player
			if (Game.BBC.ram[GAME_RAM_PLAYER_TELEPORTING] == 0) { // Not teleporting
				float fCanvasBorderX = fCanvasWidth * SCREEN_BORDER_SCALE; // Scroll if player moves within border
				float fCanvasBorderY = fCanvasHeight * SCREEN_BORDER_SCALE; // Scroll if player moves within border

				if (O.GameX < fCanvasX + fCanvasOffsetX + fCanvasBorderX) fScrollShiftX = (O.GameX - fCanvasX - fCanvasOffsetX - fCanvasBorderX);
				if (O.GameX > fCanvasX + fCanvasOffsetX + (fCanvasWidth - fCanvasBorderX - 20)) fScrollShiftX = (O.GameX - fCanvasX - fCanvasOffsetX - (fCanvasWidth - fCanvasBorderX - 20));

				if (O.GameY < fCanvasY + fCanvasOffsetY + fCanvasBorderY) fScrollShiftY = (O.GameY - fCanvasY - fCanvasOffsetY - fCanvasBorderY);
				if (O.GameY > fCanvasY + fCanvasOffsetY + (fCanvasHeight - fCanvasBorderY - 20)) fScrollShiftY = (O.GameY - fCanvasY - fCanvasOffsetY - (fCanvasHeight - fCanvasBorderY - 20));
			}
			// O------------------------------------------------------------------------------O

			// For debugging - time game loop:
			auto TimeStop_GameLoop = std::chrono::high_resolution_clock::now();
			Time_GameLoop = std::chrono::duration_cast<std::chrono::nanoseconds> (TimeStop_GameLoop - TimeStart_GameLoop);

		}
		// O++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++O
	

		// O++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++O
		// O++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++O
		// + DRAW SCREEN EVERY PGE LOOP                                                   +
		// O++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++O
		// O++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++O

		// For debugging - time draw screen:
		auto TimeStart_DrawScreen = std::chrono::high_resolution_clock::now();

		// O------------------------------------------------------------------------------O
		// | Blank screen + draw screen flash                                             |
		// O------------------------------------------------------------------------------O
		Clear(olc::BLANK);
		if (bScreenFlash) Clear(olc::WHITE);
		// O------------------------------------------------------------------------------O

		// Mode A: skip the entire HD draw pipeline; just blit the BBC's own Mode 1 framebuffer.
		// (Skipping the HD draw block also skips DetermineBackground() — fine, because Mode A
		//  doesn't apply HD patches, so the BBC's own classify code spawns enemies natively.)
		if (gMode == 'A') {
			DrawBBCFramebuffer();
			return true;
		}

		// O------------------------------------------------------------------------------O
		// | Draw background water                                                        |
		// O------------------------------------------------------------------------------O
		int nTileOffsetX = int((fCanvasX + fCanvasOffsetX) / GAME_TILE_WIDTH);
		int nTileOffsetY = int((fCanvasY + fCanvasOffsetY) / GAME_TILE_HEIGHT);

		for (int i = -1; i < (fCanvasWidth / GAME_TILE_WIDTH + 1); i++) { // Draw general water level:
			int x = i + nTileOffsetX;
			float fScreenX = ScreenCoordinateX(x * GAME_TILE_WIDTH);
			float fScreenY = ScreenCoordinateY(Game.WaterLevel(x));
			if (fScreenY < -GAME_TILE_HEIGHT) fScreenY = -GAME_TILE_HEIGHT;
			olc::PixelGameEngine::DrawDecal(olc::vf2d(fScreenX, fScreenY), decWater[0].get(), olc::vf2d(SCREEN_ZOOM, SCREEN_ZOOM));
		}
		for (int i = 0; i < Game.WaterTiles.size(); i++) { // Draw specific water tiles throughout map:
			float fScreenX = ScreenCoordinateX(Game.WaterTiles[i].GameX * GAME_TILE_WIDTH);
			float fScreenY = ScreenCoordinateY(Game.WaterTiles[i].GameY * GAME_TILE_HEIGHT);
			olc::PixelGameEngine::DrawDecal(olc::vf2d(fScreenX, fScreenY), decWaterSquare[0].get(), olc::vf2d(SCREEN_ZOOM, SCREEN_ZOOM));
		}
		// O------------------------------------------------------------------------------O

		// O------------------------------------------------------------------------------O
		// | Draw particles                                                               |
		// O------------------------------------------------------------------------------O
		uint16_t nParticleCount = Game.BBC.ram[GAME_RAM_PARTICLE_COUNT]; // number_of_particles

		if (nParticleCount != 0xFF) {
			for (int i = 0; i < nParticleCount + 1; i++) {
				ExileParticle P;
				P = Game.Particle(i);

				uint8_t nCol = P.ParticleType & 0x07;

				Game.DrawExileParticle(
					this, 
					ScreenCoordinateX(P.GameX),
					ScreenCoordinateY(P.GameY),
					SCREEN_ZOOM, 
					(P.ParticleType >> 6) & 1,
					olc::Pixel(((nCol >> 0) & 1) * 0xFF, ((nCol >> 1) & 1) * 0xFF, ((nCol >> 2) & 1) * 0xFF));
			}
		}
		// O------------------------------------------------------------------------------O

		// O------------------------------------------------------------------------------O
		// | Draw game objects                                                            |
		// O------------------------------------------------------------------------------O
		int nObjectCount = 0;
		for (int nObjectID = 0; nObjectID < OBJECT_SLOTS; nObjectID++) {
			Obj O;
			O = Game.Object(nObjectID);

			if ((nObjectID == 0) || (nObjectID == Game.BBC.ram[0xdd])) {
				// Offset the scroll shift for player (and objects held) to avoid judder
				O.GameX += fScrollShiftX * fTimeSinceLastFrame / 0.025;
				O.GameY += fScrollShiftY * fTimeSinceLastFrame / 0.025;
			}

			Game.DrawExileSprite(
				this,
				O.SpriteID,
				ScreenCoordinateX(O.GameX),
				ScreenCoordinateY(O.GameY),
				SCREEN_ZOOM,
				O.Palette,
				O.HorizontalFlip,
				O.VerticalFlip,
				O.Teleporting,
				O.Timer);

			if (Game.BBC.ram[OS_Y + nObjectID] != 0) nObjectCount++; // For debugging
		}
		// O------------------------------------------------------------------------------O

		// O------------------------------------------------------------------------------O
		// | Draw "foreground" water (transparent)                                        |
		// O------------------------------------------------------------------------------O
		for (int i = -1; i < (fCanvasWidth / GAME_TILE_WIDTH + 1); i++) { // Draw general water level:
			int x = i + nTileOffsetX;
			float fScreenX = ScreenCoordinateX(x * GAME_TILE_WIDTH);
			float fScreenY = ScreenCoordinateY(Game.WaterLevel(x));
			if (fScreenY < -GAME_TILE_HEIGHT) fScreenY = -GAME_TILE_HEIGHT;
			olc::PixelGameEngine::DrawDecal(olc::vf2d(fScreenX, fScreenY), decWater[1].get(), olc::vf2d(SCREEN_ZOOM, SCREEN_ZOOM));
		}
		for (int i = 0; i < Game.WaterTiles.size(); i++) { // Draw specific water tiles throughout map:
			float fScreenX = ScreenCoordinateX(Game.WaterTiles[i].GameX * GAME_TILE_WIDTH);
			float fScreenY = ScreenCoordinateY(Game.WaterTiles[i].GameY * GAME_TILE_HEIGHT);
			olc::PixelGameEngine::DrawDecal(olc::vf2d(fScreenX, fScreenY), decWaterSquare[1].get(), olc::vf2d(SCREEN_ZOOM, SCREEN_ZOOM));
		}
		// O------------------------------------------------------------------------------O

		// O------------------------------------------------------------------------------O
		// | Draw background map                                                          |
		// O------------------------------------------------------------------------------O
		for (int j = -1; j < (fCanvasHeight / GAME_TILE_HEIGHT + 1); j++) {
			for (int i = -1; i < (fCanvasWidth / GAME_TILE_WIDTH + 1); i++) {

				int x = i + nTileOffsetX; // Set x and clamp
				float fTileShiftX = 0;
				if (x < 0) { fTileShiftX = (x - 1) * GAME_TILE_WIDTH; x = 1; } // x = 1, rather than 0 to avoid repeating bush
				if (x > 0xFF) { fTileShiftX = (x - 0xFF) * GAME_TILE_WIDTH; x = 0xFF; }

				int y = j + nTileOffsetY; // Set y and clamp
				float fTileShiftY = 0;
				if (y < 0) { fTileShiftY = y * GAME_TILE_HEIGHT; y = 0; }
				if (y > 0xFF) { fTileShiftY = (y - 0xFF) * GAME_TILE_HEIGHT; y = 0xFF; }

				float fScreenX = ScreenCoordinateX(Game.BackgroundGrid(x, y).GameX + fTileShiftX + 1); // Why need "+1"?
				float fScreenY = ScreenCoordinateY(Game.BackgroundGrid(x, y).GameY + fTileShiftY);

				if (Game.BackgroundGrid(x, y).TileID != GAME_TILE_BLANK) {
					int VerticalFlip = (Game.BackgroundGrid(x, y).Orientation >> 6) & 1; // Check bit 6 
					int HorizontalFlip = (Game.BackgroundGrid(x, y).Orientation >> 7) & 1; // Check bit 7

					Game.DrawExileSprite(
						this,
						Game.BackgroundGrid(x, y).SpriteID,
						fScreenX, fScreenY, SCREEN_ZOOM,
						Game.BackgroundGrid(x, y).Palette,
						HorizontalFlip,
						VerticalFlip);
				}

				Game.DetermineBackground(x, y, nFrameCounter); // Called to ensure background objects are activated when in view

			}
		}

		// For debugging - time draw screen:
		auto TimeStop_DrawScreen = std::chrono::high_resolution_clock::now();
		Time_DrawScreen = std::chrono::duration_cast<std::chrono::nanoseconds> (TimeStop_DrawScreen - TimeStart_DrawScreen);

		// O------------------------------------------------------------------------------O
		// | Draw debug grid and overlay                                                  |
		// O------------------------------------------------------------------------------O
		if (bShowDebugGrid) {
			for (int j = -1; j < (fCanvasWidth / GAME_TILE_HEIGHT + 1); j++) {
				for (int i = -1; i < (fCanvasWidth / GAME_TILE_WIDTH + 1); i++) {
					int x = i + nTileOffsetX;
					float fScreenX = ScreenCoordinateX(x * GAME_TILE_WIDTH);
					int y = j + nTileOffsetY;
					float fScreenY = ScreenCoordinateY(y * GAME_TILE_HEIGHT);
					olc::PixelGameEngine::DrawStringDecal(olc::vd2d(fScreenX + 2, fScreenY + 2), hex(x, 2), olc::WHITE);
					olc::PixelGameEngine::DrawStringDecal(olc::vd2d(fScreenX + 2, fScreenY + 12), hex(y, 2), olc::WHITE);
				}
			}

			for (int i = -1; i < (fCanvasWidth / GAME_TILE_WIDTH + 1); i++) {
				int x = i + nTileOffsetX;
				float fScreenX = ScreenCoordinateX(x * GAME_TILE_WIDTH);
				olc::PixelGameEngine::FillRectDecal(olc::vd2d(fScreenX, 0), olc::vd2d(1, SCREEN_HEIGHT), olc::WHITE);
			}

			for (int j = -1; j < (fCanvasWidth / GAME_TILE_HEIGHT + 1); j++) {
				int y = j + nTileOffsetY;
				float fScreenY = ScreenCoordinateY(y * GAME_TILE_HEIGHT);
				olc::PixelGameEngine::FillRectDecal(olc::vd2d(0, fScreenY), olc::vd2d(SCREEN_WIDTH, 1), olc::WHITE);
			}
		}

		if (bShowDebugOverlay) {
			if (nObjectCount > nObjectCountMax) nObjectCountMax = nObjectCount;
			if (nParticleCount > nParticleCountMax) nParticleCountMax = nParticleCount;

			olc::PixelGameEngine::FillRectDecal(olc::vd2d(0, 0), olc::vd2d(375, 205), olc::VERY_DARK_GREY);

			olc::PixelGameEngine::DrawStringDecal(olc::vd2d(32, 32), "OBJECTS:   " + std::to_string(nObjectCount), olc::YELLOW, olc::vf2d(2.0f, 2.0f));
			olc::PixelGameEngine::DrawStringDecal(olc::vd2d(32, 52), "MAX:       " + std::to_string(nObjectCountMax), olc::YELLOW, olc::vf2d(2.0f, 2.0f));

			olc::PixelGameEngine::DrawStringDecal(olc::vd2d(32, 84), "PARTICLES: " + std::to_string(nParticleCount), olc::YELLOW, olc::vf2d(2.0f, 2.0f));
			olc::PixelGameEngine::DrawStringDecal(olc::vd2d(32, 104), "MAX:       " + std::to_string(nParticleCountMax), olc::YELLOW, olc::vf2d(2.0f, 2.0f));

			olc::PixelGameEngine::DrawStringDecal(olc::vd2d(32, 136), "TIME GAME: " + std::to_string(Time_GameLoop.count()), olc::GREEN, olc::vf2d(2.0f, 2.0f));
			olc::PixelGameEngine::DrawStringDecal(olc::vd2d(32, 156), "TIME DRAW: " + std::to_string(Time_DrawScreen.count()), olc::GREEN, olc::vf2d(2.0f, 2.0f));
		}
		// O------------------------------------------------------------------------------O
#ifdef EXILE_VARIANT_SIDEWAYS_RAM
		{
			char dbg[96]; snprintf(dbg, sizeof(dbg), "sideways: PC=$%04X  firstZeroFrom=$%04X  bank=%u",
			                      (unsigned)Game.BBC.cpu.pc, (unsigned)sw_lastPcBefore0, (unsigned)Game.BBC.activeBank);
			olc::PixelGameEngine::DrawStringDecal(olc::vd2d(10, SCREEN_HEIGHT - 30), dbg, olc::YELLOW, olc::vf2d(2.0f, 2.0f));
		}
		// Yield to macOS WindowServer so the window activates and doesn't dock-bounce.
		// PGE on macOS GLUT uses glutIdleFunc (no rate-limit, no vsync), so without this
		// we peg 100% CPU and the window never registers properly on macOS 26.
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
#endif

		return true;
	}

};

int main(int argc, char* argv[])
{
	// Parse `--mode A|B|C` (default 'C' = HD renderer, current behaviour)
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--mode" && i + 1 < argc) {
			gMode = (char)std::toupper((unsigned char)argv[++i][0]);
		}
	}

	Exile_olc6502_HD exile;
	exile.Construct(SCREEN_WIDTH, SCREEN_HEIGHT, 1, 1, SCREEN_FULLSCREEN, SCREEN_VSYNC);
	exile.Start();
	return 0;
}