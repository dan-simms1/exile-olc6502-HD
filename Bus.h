#pragma once
#include <cstdint>
#include "olc6502.h"
#include <array>

class BBCSound;  // forward decl — Bus only needs the pointer.

class Bus
{
public:
	Bus();
	~Bus();

public: // Devices on Bus
	olc6502 cpu;
	std::array<uint8_t, 64 * 1024> ram;

	// BBC Master sideways-RAM / ROM slots (16 banks × 16 KB, paged at $8000-$BFFF).
	// Active bank selected by writing to ROMSEL at $FE30 (low 4 bits).
	// Only the sideways-RAM variant touches this; standard build simply ignores it
	// (the standard BBC Micro has only one ROM permanently mapped at $8000 and HD
	// code never writes to $FE30).
	std::array<std::array<uint8_t, 16 * 1024>, 16> sidewaysBanks{};
	uint8_t activeBank = 0;
	bool bSidewaysPaging = false;  // Opt-in: only the sideways-RAM variant enables this. Standard HD
	                               // build leaves it false so $8000-$BFFF behaves as flat main RAM
	                               // (it uses that range for relocated object stacks).

	// SN76489 sound chip emulator. Optional — if null, sound writes are
	// silently absorbed. Owned by Main; Bus just forwards System VIA writes.
	BBCSound* sound = nullptr;

	// Video ULA palette: logical colour (0-15) → raw physical nibble as written to $FE21.
	// The low 3 bits are inverted (0 = on) and encode B,G,R (bit0=Blue, bit1=Green, bit2=Red);
	// Mode A renderer XORs with 0x07 before mapping to RGB. Pre-seed to "all-black" default
	// (raw nibble 0x07 = BGR all off) so the first frame before any $FE21 write isn't white.
	std::array<uint8_t, 16> videoULAPalette{{0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
	                                          0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07}};

	// CRTC scroll state: R12/R13 hold the 14-bit MA (memory-address) start. Exile uses a
	// custom 16 KB screen at &4000-&7FFF → MA = 0x0800 (byte address = MA*8 = 0x4000).
	// Pre-set so the first Mode A render works before the game (post-boot snapshot) touches
	// the CRTC; Modes B/C don't look at crtcR12/R13.
	uint8_t  crtcSelectedReg = 0;     // which CRTC register is being written ($FE00 selects)
	uint8_t  crtcR12 = 0x08;          // MA high byte (Exile screen at $4000)
	uint8_t  crtcR13 = 0x00;          // MA low byte

public: // Bus read and write
	void write(uint16_t addr, uint8_t data);
	uint8_t read(uint16_t addr, bool bReadOnly = false);
};
