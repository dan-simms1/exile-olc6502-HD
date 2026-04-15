#include "Bus.h"

Bus::Bus()
{
	// Clear RAM contents
	for (auto& i : ram) i = 0x00;
	// Match real hardware: an empty sideways slot reads as 0xFF (pulled-high data bus on
	// an unoccupied ROM socket). If this is left as 0x00 any stray read here decodes as BRK
	// and spirals via an all-zero IRQ vector.
	for (auto& bank : sidewaysBanks) for (auto& b : bank) b = 0xFF;
	activeBank = 0;

	// Connect CPU to communication bus
	cpu.ConnectBus(this);
}

Bus::~Bus()
{
}

void Bus::write(uint16_t addr, uint8_t data)
{
	if (bSidewaysPaging) {
		// ROMSEL: writing to $FE30 selects which sideways bank is paged in at $8000-$BFFF.
		if (addr == 0xFE30 || addr == 0xFE34) {
			activeBank = data & 0x0F;
			ram[addr] = data;
			return;
		}
		// Paged window: writes to $8000-$BFFF go to the active sideways bank.
		if (addr >= 0x8000 && addr <= 0xBFFF) {
			sidewaysBanks[activeBank][addr - 0x8000] = data;
			return;
		}
	}

	// All other addresses — main RAM. (Standard HD build: entire 64 KB is flat RAM,
	// which is how the project uses $9600-$A800 for its relocated object stacks.)
	ram[addr] = data;
}

uint8_t Bus::read(uint16_t addr, bool bReadOnly)
{
	if (bSidewaysPaging && addr >= 0x8000 && addr <= 0xBFFF) {
		return sidewaysBanks[activeBank][addr - 0x8000];
	}
	return ram[addr];
}
