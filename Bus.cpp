#include "Bus.h"
#include "BBCSound.h"

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

	// Video ULA palette register ($FE21) — mode A framebuffer renderer reads this
	// to map BBC logical colours to physical colours.
	if (addr == 0xFE21) {
		uint8_t logical  = (data >> 4) ^ 0x0F;  // top nibble inverted = logical colour index
		uint8_t physical = data & 0x0F;          // bottom nibble = physical colour (BGR bits)
		videoULAPalette[logical] = physical;
		// Falls through to ram[addr] = data for consistency with sound writes
	}

	// CRTC register selection ($FE00) and data ($FE01) — mode A needs to track
	// R12:R13 (screen start address) to handle CRTC scroll within the circular screen buffer.
	if (addr == 0xFE00) {
		crtcSelectedReg = data & 0x1F;  // low 5 bits select the register
	}
	if (addr == 0xFE01) {
		if (crtcSelectedReg == 12) crtcR12 = data;
		else if (crtcSelectedReg == 13) crtcR13 = data;
		// Falls through to ram[addr] = data
	}

	// System VIA → SN76489 path. The game writes its sound byte to port A
	// ($FE4F) then strobes the IC32 latch (port B at $FE40) with line 0
	// going low to assert sound chip /WE. BBCSound::OnPortBWrite latches
	// the byte on the falling edge.
	if (sound) {
		if (addr == 0xFE4F) sound->OnPortAWrite(data);
		else if (addr == 0xFE40) sound->OnPortBWrite(data);
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
