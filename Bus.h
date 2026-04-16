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

public: // Bus read and write
	void write(uint16_t addr, uint8_t data);
	uint8_t read(uint16_t addr, bool bReadOnly = false);
};
