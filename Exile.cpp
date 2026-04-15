#include <iterator>
#include "Exile.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

bool Exile::ParseAssemblyLine(std::string sLine) {
	static uint16_t nRam = 0x00;
	static std::array<bool,65536> bWritten{}; // first-write-wins to ignore alt/self-mod annotations in newer disassemblies

	// Split line, assuming space delimited
	std::istringstream iss(sLine);
	std::vector<std::string> sLineParsed((std::istream_iterator<std::string>(iss)), std::istream_iterator<std::string>());

	if (sLineParsed.size() > 0) {
		for (int nWord = 0; nWord < sLineParsed.size(); nWord++) {
			std::string sWord = sLineParsed[nWord];
			if (sWord == ";" || sWord == "#") nWord = sLineParsed.size(); // Skip line
			else if (nWord == 0) {
				// Accept either "#XXXX:" (length 6) or "&XXXX" (length 5) as address marker
				bool bValid = (sWord.length() == 6 || sWord.length() == 5)
				              && (sWord.front() == '#' || sWord.front() == '&');
				if (!bValid) { nWord = sLineParsed.size(); }
				else {
					// Trim leading # or &
					sWord.erase(0, 1);
					// If length is now 5, there's a trailing ':' to strip
					if (sWord.length() == 5) sWord.erase(4, 1);
					// Only 4 hex chars should remain
					if (sWord.length() != 4) { nWord = sLineParsed.size(); }
					else { try { nRam = std::stoi(sWord, nullptr, 16); } catch (...) { nWord = sLineParsed.size(); } }
				}
			}
			else {
				if (sWord.length() != 2) nWord = sLineParsed.size(); // Skip line, as reached end of hex
				else if (sWord == "--") nWord = sLineParsed.size(); // Skip line, as hex not defined
				else {
					try { size_t pos=0; int v=std::stoi(sWord,&pos,16); if (pos!=sWord.length()) { nWord=sLineParsed.size(); } else { if (!bFirstWriteWins || !bWritten[nRam]) { BBC.ram[nRam]=(uint8_t)v; bWritten[nRam]=true; } nRam++; } } catch (...) { nWord=sLineParsed.size(); }
				}
			}
		}
	}

	return true;
}

bool Exile::LoadExileFromBinary(std::string sFile, uint16_t loadAddr) {
	std::ifstream f(sFile, std::ios::binary);
	if (!f.is_open()) { std::cout << "ROM file missing: " << sFile << std::endl; return false; }
	std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	for (size_t i = 0; i < buf.size() && (loadAddr + i) < 0x10000; i++) {
		BBC.ram[loadAddr + i] = buf[i];
	}
	std::cout << "Loaded " << buf.size() << " bytes from " << sFile << " at 0x" << std::hex << loadAddr << std::dec << std::endl;
	return true;
}

// Load a ROM file into a specific sideways-RAM bank, at an offset within that bank.
// The paged window is $8000-$BFFF, so offsetInBank = rom_load_addr - 0x8000.
bool Exile::LoadExileFromBinaryToBank(std::string sFile, uint8_t bank, uint16_t offsetInBank) {
	std::ifstream f(sFile, std::ios::binary);
	if (!f.is_open()) { std::cout << "ROM file missing: " << sFile << std::endl; return false; }
	std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	bank &= 0x0F;
	for (size_t i = 0; i < buf.size() && (offsetInBank + i) < BBC.sidewaysBanks[bank].size(); i++) {
		BBC.sidewaysBanks[bank][offsetInBank + i] = buf[i];
	}
	std::cout << "Loaded " << buf.size() << " bytes from " << sFile
	          << " into sideways bank " << (int)bank
	          << " at offset 0x" << std::hex << offsetInBank << std::dec << std::endl;
	return true;
}

bool Exile::LoadExileFromDisassembly(std::string sFile) {
	bFirstWriteWins = true; // enable only for disassembly load — ignores self-mod alt annotations in newer files
	// Load Exile RAM from disassembly:
	std::string sLine;
	std::ifstream fileExileDisassembly(sFile);
	if (fileExileDisassembly.is_open())
	{
		bool bLoadStarted = false;

		while (getline(fileExileDisassembly, sLine))
		{
			if ((sLine.substr(0, 5) == "#0100") || (sLine.substr(0, 5) == "&0100") || bLoadStarted) {
				bLoadStarted = true;
				ParseAssemblyLine(sLine);
			}
		}

		fileExileDisassembly.close();
	}
	else {
		std::cout << "Exile disassembly file missing";
		return false;
	}

	// Corrections needed for current level7.org.uk disassembly.
	BBC.ram[0x34C6] = 0x18; BBC.ram[0x34C7] = 0x18;
	BBC.ram[0x492E] = 0x82; BBC.ram[0x492F] = 0xA9; BBC.ram[0x4930] = 0x4B;

	bFirstWriteWins = false; // allow PatchExileRAM to overwrite freely
	return true;
}

void Exile::CopyRAM(uint16_t nSource, uint16_t nTarget, uint8_t nLength) {
	for (int i = 0; i < nLength; i++) {
		BBC.ram[nTarget + i] = BBC.ram[nSource + i];
	}
}

void Exile::PatchExileRAM() {
	std::string P = "";

	// Relocate primary object stacks, to give space for 128 objects
	CopyRAM(0x0860, 0x9600, 0x10);  // object_stack_type
	CopyRAM(0x0870, 0x9700, 0x10);  // object_stack_sprite
	CopyRAM(0x0880, 0x9800, 0x10);  CopyRAM(0x0890, 0x9880, 0x01);  // object_stack_x_low
	CopyRAM(0x0891, 0x9900, 0x10);  CopyRAM(0x08a1, 0x9980, 0x02);  // object_stack_x
	CopyRAM(0x08a3, 0x9a00, 0x10);  CopyRAM(0x08b3, 0x9a80, 0x01);  // object_stack_y_low
	CopyRAM(0x08b4, 0x9b00, 0x10);  CopyRAM(0x08c4, 0x9b80, 0x02);  // object_stack_y
	CopyRAM(0x08c6, 0x9c00, 0x10);  // object_stack_flags
	CopyRAM(0x08d6, 0x9d00, 0x10);  // object_stack_palette
	CopyRAM(0x08e6, 0x9e00, 0x10);  // object_stack_vel_x
	CopyRAM(0x08f6, 0x9f00, 0x10);  // object_stack_vel_y
	P += "#a000: a0 00 e0 e0 a0 a0 a0 a0 a0 a0 20 80 20 20 20 00 \n"; // object_stack_target
	P += "#a800: 10 00 10 10 11 11 11 11 10 10 11 10 11 10 11 02 \n"; // object_stack_target_object (new stack)
	CopyRAM(0x0916, 0xa100, 0x10);  // object_stack_tx
	CopyRAM(0x0926, 0xa200, 0x10);  // object_stack_energy
	CopyRAM(0x0936, 0xa300, 0x10);  // object_stack_ty
	CopyRAM(0x0946, 0xa400, 0x10);  // object_stack_supporting
	CopyRAM(0x0956, 0xa500, 0x10);  // object_stack_timer
	CopyRAM(0x0966, 0xa600, 0x10);  // object_stack_data_pointer
	CopyRAM(0x0976, 0xa700, 0x10);  // object_stack_extra

	// Various updates to code to refer to new stack locations
	// Note: Currently, there is also a 'redirect' to the new stack locations in olc6502
	// To do: update patched code here, so that the redirect is not required

	P += "&1a62: 4c 10 ff   JMP &ff10       ; JUMP TO PATCHED CODE \n";
	P += "&ff10: bd 06 09   LDA &0906, X    ; object_stack_target \n"; // (only need bits 5 to 7) 
	P += "&ff13: 85 3e      STA &3e         ; this_object_target \n";
	P += "&ff15: 85 3f      STA &3f         ; this_object_target_old \n";
	P += "&ff17: bd 00 a8   LDA &a800, X    ; object_stack_target_object \n"; // (new stack)
	P += "&ff1a: 85 0e      STA &0e         ; this_object_target_object \n";
	P += "&ff1c: 4c 6d 1a   JMP 1a6d        ; JUMP BACK \n";

	P += "&1a8f: 4c 00 ff   JMP ff00        ; JUMP TO PATCHED CODE \n";
	P += "&ff00: 29 0f      AND #&0f \n"; 
	P += "&ff01: 0a         ASL \n";
	P += "&ff02: 0a         ASL \n";
	P += "&ff03: 0a         ASL \n";
	P += "&ff04: 0a         ASL \n";
	P += "&ff05: 4c 95 1a   JML 1a95        ; JUMP BACK \n";

	P += "&1dbe: 4c 20 ff   JMP ff20        ; JUMP TO PATCHED CODE \n";
	P += "&ff20: a5 3e      LDA &3e         ; this_object_target \n";
	P += "&ff22: 9d 06 09   STA &0906, X    ; object_stack_target \n";
	P += "&ff25: a5 0e      LDA &0e         ; this_object_target_object \n";
	P += "&ff27: 9d 00 a8   STA &a800, X    ; this_object_target_object \n";
	P += "&ff2a: 4c c7 1d   JMP 1dc7        ; JUMP BACK \n";

	P += "&1e34: 5d 00 a8   EOR &a800, X   ; object_stack_target_*OBJECT* \n"; // Updated 1 Jan 2021

//	P += "&1e3c: 9d 00 a8   STA &a800, X; object_stack_target_object \n";
	P += "&1e3c: 4c 00 fe   JMP fe00        ; JUMP TO PATCHED CODE \n";
	P += "&fe00: 9d 00 a8   STA &a800, X    ; object_stack_target_object \n";
	P += "&fe03: 8d 0c fe   STA &fe0c       ; MODIFYING CODE BELOW \n";
	P += "&fe06: A9 00      LDA &#00        ; Zero \n";
	P += "&fe08: 9d 00 a0   STA &a000, X    ; object_stack_target \n";
	P += "&fe0b: A9 00      LDA &#00        ; SELF MOD CODE \n";
	P += "&fe0d: 4c 3f 1e   JMP 1e3f        ; JUMP BACK \n";

//	P += "&1edf: 99 00 a8     STA &a800, Y; object_stack_target_object \n";
	P += "&1edf: 4c 10 fe   JMP fe00        ; JUMP TO PATCHED CODE \n";
	P += "&fe10: 99 00 a8   STA &a800, Y    ; object_stack_target_object \n";
	P += "&fe13: 8d 1c fe   STA &fe0c       ; MODIFYING CODE BELOW \n";
	P += "&fe16: A9 00      LDA &#00        ; Zero \n";
	P += "&fe18: 99 00 a0   STA &a000, Y    ; object_stack_target \n";
	P += "&fe1b: A9 00      LDA &#00        ; SELF MOD CODE \n";
	P += "&fe1d: 4c e2 1e   JMP 1ee2        ; JUMP BACK \n";

//	P += "&27b7: 9d 00 a8     STA &a800, X; object_stack_target_object \n";
	P += "&27b7: 4c 20 fe   JMP fe20        ; JUMP TO PATCHED CODE \n";
	P += "&fe20: 9d 00 a8   STA &a800, X    ; object_stack_target_object \n";
	P += "&fe23: 8d 2c fe   STA &fe0c       ; MODIFYING CODE BELOW \n";
	P += "&fe26: A9 00      LDA &#00        ; Zero \n";
	P += "&fe28: 9d 00 a0   STA &a000, X    ; object_stack_target \n";
	P += "&fe2b: A9 00      LDA &#00        ; SELF MOD CODE \n";
	P += "&fe2d: 4c ba 27   JMP 27ba        ; JUMP BACK \n";

//	P += "&4bfb: 9d 00 a8     STA &a800, X; object_stack_target_object \n";
	P += "&4bfb: 4c 30 fe   JMP fe30        ; JUMP TO PATCHED CODE \n";
	P += "&fe30: 9d 00 a8   STA &a800, X    ; object_stack_target_object \n";
	P += "&fe33: 8d 3c fe   STA &fe0c       ; MODIFYING CODE BELOW \n";
	P += "&fe36: A9 00      LDA &#00        ; Zero \n";
	P += "&fe38: 9d 00 a0   STA &a000, X    ; object_stack_target \n";
	P += "&fe3b: A9 00      LDA &#00        ; SELF MOD CODE \n";
	P += "&fe3d: 4c fe 4b   JMP 4bfe        ; JUMP BACK \n";

	P += "&2a7e: b9 80 98   LDA &9880, Y    ; (object_stack_x) \n";
	P += "&2a89: b9 80 9a   LDA &9a80, Y    ; (object_stack_y) \n";
	P += "&2a99: be 80 96   LDX &9680, Y    ; (object_stack_sprite) \n";
	P += "&2aa6: b9 80 9a   LDA &9a80, Y    ; (object_stack_y) \n";
	P += "&2aab: b9 80 99   LDA &9980, Y    ; (object_stack_y_low) \n";
	P += "&2aeb: b9 80 98   LDA &9880, Y    ; (object_stack_x) \n";
	P += "&2af0: b9 80 97   LDA &9780, Y    ; (object_stack_x_low) \n";
	P += "&2b1d: 19 80 a3   ORA &a380, Y	; (object_stack_supporting) \n";
	P += "&2b24: 99 80 a3   STA &a380, Y    ; (object_stack_supporting) \n";
	P += "&2b27: be 80 95   LDX &9580, Y    ; (object_stack_type) \n";
	P += "&2b9b: b9 80 9d   LDA &9d80, Y    ; (object_stack_vel_x) \n";
	P += "&2ba3: 99 80 9d   STA &9d80, Y    ; (object_stack_vel_x) \n";
	P += "&2ba8: b9 80 9e   LDA &9e80, Y    ; (object_stack_vel_y) \n";
	P += "&2bb0: 99 80 9e   STA &9e80, Y    ; (object_stack_vel_y) \n";

	P += "&3ced: 4c 40 ff   JMP ff40        ; JUMP TO PATCHED CODE \n";
	P += "&ff40: a2 00      LDX #&00        ; zero \n";
	P += "&ff42: 86 3e      STX &3e         ; this_object_target \n";
	P += "&ff44: a6 aa      LDX &aa         ; current_object \n";
	P += "&ff46: 86 0e      STX &0e         ; this_object_target_object \n";
	P += "&ff48: 4c f3 3c   JMP 3cf3        ; JUMP BACK \n";

	// Increasing primary stack to 128 objects:
	P += "&1e11: e0 80      CPX #&80        ; As now up to 128 objects \n";
	P += "&1e29: a2 7f      LDX #&7f        ; Start at object 127 \n";
	P += "&1e70: a0 7f      LDY #&7f        ; Start at object 127 \n";
	P += "&1eb8: c0 80      CPY #&80        ; As now up to 128 objects \n";
	P += "&288e: a2 80      LDX #&80 \n"    ; // Updated stack item used for targeting
	P += "&2a78: a0 7f      LDY #&7f        ; Start at object 127 \n";
	P += "&2a93: 69 80      ADC #&80        ; As now up to 128 objects \n";
	P += "&2afb: 69 80      ADC #&80        ; As now up to 128 objects \n"; // Needed?
	P += "&3442: a2 7f      LDX #&7f        ; As now up to 128 objects \n";
	P += "&3c4d: 29 7f      AND #&7f        ; As now up to 128 objects \n"; // Added 1 Jan 2021
	P += "&3c51: a0 7f      LDY #&7f        ; As now up to 128 objects \n";
	P += "&609e: a0 7f      LDY #&7f        ; As now up to 128 objects \n"; // Not used?

	// Relocating and restructuring particle stack, to give space for 127 particles
	// And some updates, to ensure pixels are processed, even if off the BBC screen
	P += "&202b: bd 00 88   LDA &8800, X    ; particle_stack_x_low \n";
	P += "&2032: bd 00 8a   LDA &8a00, X    ; particle_stack_x \n";
	P += "&2037: 18         CLC             ; Always process pixel \n";
	P += "&2038: 18         CLC             ; Always process pixel \n";
	P += "&203d: bd 00 89   LDA &8900, X    ; particle_stack_y_low \n";
	P += "&2045: bd 00 8b   LDA &8b00, X    ; particle_stack_y \n";
	P += "&2057: 18         CLC             ; Always process pixel \n";
	P += "&2058: 18         CLC             ; Always process pixel \n";
	P += "&2097: bd 00 8d   LDA 8d00, X     ; particle_stack_type \n";
	P += "&20ad: bd 00 8b   LDA &8b00, X    ; particle_stack_y \n";
	P += "&20b7: bd 00 89   LDA &8900, X    ; particle_stack_y_low \n";
	P += "&20cb: 7d 00 87   ADC &8700, X    ; particle_stack_velocity_y \n";
	P += "&20d0: 9d 00 87   STA &8700, X    ; particle_stack_velocity_y \n";
	P += "&20e1: de 00 8c   DEC &8c00, X    ; particle_stack_ttl \n";

	P += "&20e6: 4c 00 93   JMP 9300        ; JUMP TO PATCHED CODE \n";
	P += "&9300: 18         CLC \n";
	P += "&9301: bd 00 86   LDA & 8600, X   ; particle_stack_velocity_x \n";
	P += "&9304: 48         PHA \n";
	P += "&9305: 7d 00 88   ADC & 8800, X   ; particle_stack_x_low \n";
	P += "&9308: 9d 00 88   STA & 8800, X   ; particle_stack_x_low \n";
	P += "&930b: 90 03      BCC & 9310 \n";
	P += "&930d: fe 00 8a   INC & 8a00, X   ; particle_stack_x \n";
	P += "&9310: 68         PLA \n";
	P += "&9311: 10 03      BPL & 9316 \n";
	P += "&9313: de 00 8a   DEC & 8a00, X   ; particle_stack_x \n";
	P += "&9316: 18         CLC \n";
	P += "&9317: bd 00 87   LDA & 8700, X   ; particle_stack_velocity_y \n";
	P += "&931a: 48         PHA \n";
	P += "&931b: 7d 00 89   ADC & 8900, X   ; particle_stack_y_low \n";
	P += "&931e: 9d 00 89   STA & 8900, X   ; particle_stack_y_low \n";
	P += "&9321: 90 03      BCC & 9326 \n";
	P += "&9323: fe 00 8b   INC & 8b00, X   ; particle_stack_y \n";
	P += "&9326: 68         PLA \n";
	P += "&9327: 10 03      BPL & 932c \n";
	P += "&9329: de 00 8b   DEC & 8b00, X   ; particle_stack_y \n";
	P += "&932c: a0 01      LDY #&01        ; Just in case \n";
	P += "&932e: 88         DEY             ; Just in case \n";
	P += "&932f: 88         DEY             ; Just in case \n";
	P += "&9330: 4c 02 21   JMP 2102        ; JUMP BACK \n";

	P += "&210a: 9d 00 8d   STA &8d00, X    ; particle_stack_type \n";
	P += "&2124: e9 01      SBC #&01 \n";

	P += "&213d: 4c 00 94   JMP 9400       ; JUMP TO PATCHED CODE \n";
	P += "&9400: b9 00 86   LDA & 8600, Y  ; particle_stack_velocity_x \n";
	P += "&9403: 9d 00 86   STA & 8600, X \n";
	P += "&9406: b9 00 87   LDA & 8700, Y  ; particle_stack_velocity_y \n";
	P += "&9409: 9d 00 87   STA & 8700, X \n";
	P += "&940c: b9 00 88   LDA & 8800, Y  ; particle_stack_x_low \n";
	P += "&940f: 9d 00 88   STA & 8800, X \n";
	P += "&9412: b9 00 89   LDA & 8900, Y  ; particle_stack_y_low \n";
	P += "&9415: 9d 00 89   STA & 8900, X \n";
	P += "&9418: b9 00 8a   LDA & 8a00, Y  ; particle_stack_x \n";
	P += "&941b: 9d 00 8a   STA & 8a00, X \n";
	P += "&941e: b9 00 8b   LDA & 8b00, Y  ; particle_stack_y \n";
	P += "&9421: 9d 00 8b   STA & 8b00, X \n";
	P += "&9424: b9 00 8c   LDA & 8c00, Y  ; particle_stack_ttl \n";
	P += "&9427: 9d 00 8c   STA & 8c00, X \n";
	P += "&942a: b9 00 8d   LDA & 8d00, Y  ; particle_stack_type \n";
	P += "&942d: 9d 00 8d   STA & 8d00, X \n";
	P += "&9430: 4c 4a 21   JMP 214a       ; JUMP BACK \n";

	P += "&2150: e9 01      SBC #&01 \n";
	P += "&2160: e0 7e      CPX #&7e       ; Max 127 particles \n";

	P += "&2168: 8e 59 1e   STX & 1e59     ; number_of_particles_x8 \n";
	P += "&216b: 8a         TXA \n";
	P += "&216c: 85 9e      STA &9e \n";
	P += "&216e: 0a         ASL            ; To ensure carry is clear \n"; // Needed?
	P += "&216f: 8a         TXA \n";
	P += "&2170: 8a         TXA \n";
	P += "&2171: 8a         TXA \n";
	P += "&2172: 90 17      BCC &218b      ; Branch always \n";

	P += "&2176: 29 7f      AND #&7f \n";

	P += "&217b: bd 00 8d   LDA &8d00, X   ; particle_stack_type \n";

	P += "&21ad: c9 00      CMP #&00       ; Screen size \n";
	P += "&21b8: c9 ff      CMP #&ff       ; Screen size \n";
	P += "&2229: 9d 00 8c   STA &8c00, X   ; particle_stack_ttl \n";

	P += "&224f: 8e 55 22   STX $2255      ; store x jump location in $2255 (Self modifying code) \n";
	P += "&2252: a6 9c      LDX & 9c \n";
	P += "&2254: 4c 00 90   JMP & 90XX     ; JUMP TO CORRESPONDING PATCH CODE \n";

	P += "; X = 00: If we jump here -> working with Y: \n";
	P += "&9000: 9d 00 8b   STA &8b00, X  ; particle_stack_y \n";
	P += "&9003: 68         PLA \n";
	P += "&9004: 9d 00 89   STA &8900, X  ; particle_stack_y_low \n";
	P += "&9007: 68         PLA \n";
	P += "&9008: 9d 00 87   STA &8700, X  ; particle_stack_velocity_y \n";
	P += "&900b: 4c 5e 22   JMP &225e     ; JUMP BACK \n";

	P += "; X = fe: If we jump here -> working with Y: \n";
	P += "&90fe: 9d 00 8a   STA &8a00, X  ; particle_stack_x \n";
	P += "&9101: 68         PLA \n";
	P += "&9102: 9d 00 88   STA &8800, X  ; particle_stack_x_low \n";
	P += "&9105: 68         PLA \n";
	P += "&9106: 9d 00 86   STA &8600, X  ; particle_stack_velocity_x \n";
	P += "&9109: 4c 5e 22   JMP &225e     ; JUMP BACK \n";

	P += "&2270: 4c 00 92   JMP 9200       ; JUMP TO PATCHED CODE \n";

	P += "&9200: bd 00 87   LDA &8700, X  ; particle_stack_velocity_y \n";
	P += "&9203: 79 43 00   ADC &0043, Y \n";
	P += "&9206: 20 7f 32   JSR &327f     ; prevent_overflow \n";
	P += "&9209: 9d 00 87   STA &8700, X  ; particle_stack_velocity_y \n";
	P += "&920c: 88         DEY \n";
	P += "&920d: 88         DEY \n";

	P += "&920e: bd 00 86   LDA &8600, X; particle_stack_velocity_x \n";
	P += "&9211: 79 43 00   ADC &0043, Y \n";
	P += "&9214: 20 7f 32   JSR &327f; prevent_overflow \n";
	P += "&9217: 9d 00 86   STA &8600, X; particle_stack_velocity_x \n";
	P += "&921a: 88         DEY \n";
	P += "&921b: 88         DEY \n";

	P += "&921c: 4c 83 22   JMP 2283 ;       JUMP BACK \n";

	P += "&26c8: 20 50 ff   JSR &ff50; JUMP TO PATCHED SUB [Call it 9 times, as a bigger area to cover!] \n";
	P += "&26cb: 20 50 ff   JSR &ff50; \n";
	P += "&26ce: 20 50 ff   JSR &ff50; \n";
	P += "&26d1: 20 50 ff   JSR &ff50; \n";
	P += "&26d4: 20 50 ff   JSR &ff50; \n";
	P += "&26d7: 20 50 ff   JSR &ff50; \n";
	P += "&26da: 20 50 ff   JSR &ff50; \n";
	P += "&26dd: 20 50 ff   JSR &ff50; \n";
	P += "&26e0: 20 50 ff   JSR &ff50; \n";

	P += "&26e3: a0 4d                 ; JUST FILLER \n";
	P += "&26e5: 88 \n";

	P += "&ff50: a9 3f      LDA #&3f - Big star area \n";
	P += "&ff52: 20 43 27   JSR &2743; get_random_square_near_player \n";
	P += "&ff55: c9 4e      CMP #&4e ; # are we above y = &4e ? Check before determining background \n";
	P += "&ff57: b0 1d      BCS &ff73; no_stars \n";
	P += "&ff59: 20 15 17   JSR &1715; determine_background \n";
	P += "&ff5c: a5 97      LDA &97; square_y; COPIED FROM 26c8 - 26e3 (except two lines above) \n";
	CopyRAM(0x26ce, 0xff5e, 0x18);  // no_emerging_objects subroutine
	P += "&ff76: 60         RTS \n";

	P += "&273d: 4c 30 ff   JMP ff30        ; JUMP TO PATCHED CODE \n";
	
	P += "&ff30: a9 c0      LDA #&c0 \n";
	P += "&ff32: 99 06 09   STA &0906, Y    ; object_stack_target \n";
	P += "&ff35: a9 00      LDA #&00 \n";
	P += "&ff37: 99 00 a8   STA &a800, Y    ; this_object_target_object \n";
	
	P += "&ff3a: 4c 42 27   JMP 2742        ; JUMP BACK \n";

	// Radius:
	P += "&1143f: e6 9b     INC & 9b; radius \n";
	P += "& 1145: e6 9b     INC & 9b; radius \n";

	P += "&0c5a: a0 0a      LDY #&0a \n";
	P += "#19a7: 0a 0f 0a           ; funny_table_19a7 \n"; 

	// HD code patch at $34C6 (CLC CLC instead of BCS) — required for HD rendering, applied here so it works for both binary and disassembly loaders:
	BBC.ram[0x34C6] = 0x18; BBC.ram[0x34C7] = 0x18;

	// Turning off BBC sprite/background plotting:
	P += "&0ca5: 4c c0 0c   JUMP 0cc0 \n"; // Objects
	P += "&10d2: 4c ed 10   JUMP 10ed \n"; // Background strip

	std::istringstream iss(P);
	std::string sLine;
	while (getline(iss, sLine)) ParseAssemblyLine(sLine);
}

// Enhanced/sideways-ROM peer of PatchExileRAM. Applies the HD patches that
// DO translate to the enhanced ROM, sets up IRQ fake vectors, and (TODO)
// relocates sound samples into a sideways bank.
//
// The 21 address-specific HD patches in PatchExileRAM() do NOT apply here:
// enhanced has different instructions at those addresses (confirmed by
// byte-compare in MEMORY-AUDIT.md). The particle-restructure and 128-object
// expansion are also skipped — enhanced natively uses an 8-byte-interleaved
// particle layout at $2907+N*8 and keeps 16 object slots at $0860+.
//
// Radius bump (std has INC $9B at $111F/$1121) and stars widescreen
// ($26C8-$26E3 rewrite) are NOT YET ported — enhanced has different
// structure at those sites and needs dedicated research.
void Exile::PatchEnhancedExileRAM() {
	// Empty OS-ROM region: plant an RTI at $FFF0 and aim every 6502 vector
	// at it so stray BRKs return cleanly instead of spiralling through
	// zero-filled OS-ROM space (we don't load MOS).
	BBC.ram[0xFFF0] = 0x40;                                  // RTI
	BBC.ram[0xFFFA] = 0xF0; BBC.ram[0xFFFB] = 0xFF;          // NMI   → $FFF0
	BBC.ram[0xFFFC] = 0xF0; BBC.ram[0xFFFD] = 0xFF;          // RESET → $FFF0
	BBC.ram[0xFFFE] = 0xF0; BBC.ram[0xFFFF] = 0xFF;          // IRQ/BRK → $FFF0

	// PORTABLE HD patches — verified to have same address+bytes in both ROMs:
	//   $0CA5 → JMP $0CC0 : skip BBC object sprite plotter (we render via PGE).
	BBC.ram[0x0CA5] = 0x4C; BBC.ram[0x0CA6] = 0xC0; BBC.ram[0x0CA7] = 0x0C;
	//   $10D2 → JMP $10ED : skip BBC background tile plotter.
	BBC.ram[0x10D2] = 0x4C; BBC.ram[0x10D3] = 0xED; BBC.ram[0x10D4] = 0x10;
	//   $0C5B = $0A : HD tweak, LDY #$04 → LDY #$0A. Same address both ROMs.
	BBC.ram[0x0C5B] = 0x0A;

	// ENHANCED-SPECIFIC patches — different address from standard but same purpose:
	//   Sprite-too-tall BCS → CLC CLC. Std uses $34C6; enhanced's equivalent
	//   (LDA $BE89,X / CMP #$38 / BCS $3594) sits at $352D.
	BBC.ram[0x352D] = 0x18; BBC.ram[0x352E] = 0x18;

	// NOP out the copy-protection JSR at $6540 in SINIT2 so we can run the
	// rest of SINIT2 (game-state decrypt + VIA setup + JMP $01D0 wipe_screen_and_start_game)
	// without getting stuck waiting for the player to type a word from the novella.
	//   Original: 20 BE 66    JSR $66BE ; copy_protection_screen
	//   Patched:  EA EA EA    NOP NOP NOP
	BBC.ram[0x6540] = 0xEA;
	BBC.ram[0x6541] = 0xEA;
	BBC.ram[0x6542] = 0xEA;

	// Also NOP the second copy-protection check at $2740 JSR $39B5 (called when
	// summoning Triax) so mid-game doesn't hang either.
	BBC.ram[0x2740] = 0xEA;
	BBC.ram[0x2741] = 0xEA;
	BBC.ram[0x2742] = 0xEA;

	// Note: we tried running SINIT2 from $6495, but it gets stuck at its
	// decrypt-verification loop at $64FF-$6504 (infinite_loop_if_fallback_
	// teleport_not_as_expected). The decrypt at $64CD uses BCD (SED+ADC) with
	// non-BCD operands ($6E as a key byte); real-6502 behaviour on non-BCD
	// operands is hardware-specific and emulators disagree. Since standard
	// doesn't run this (bmain.rom is pre-decrypted at build time), and
	// we don't need the decrypt for display, skip SINIT2 entirely.

	// TODO — port these HD patches to enhanced once their addresses are located:
	//   [ ] Radius bump: std has INC $9B at $111F/$1121. Enhanced has zero
	//       occurrences of that opcode — need to find where enhanced manages
	//       sprite-plotting radius (probably different ZP var or different
	//       increment mechanism).
	//   [ ] Stars widescreen: std rewrites $26C8-$26E3 with 9x JSR $FF50 for
	//       a wider spawn area. Enhanced $26C8 is worm-spawning code; enhanced
	//       star-drawing starts at $26F7 with completely different structure.
	//   [ ] funny_table: enhanced equivalent is at $19CB. Bytes are already
	//       identical to the HD-patched standard (`01 0c 04 00 00 02`), so
	//       nothing to write — documented for completeness.
	//
	// TODO — sound-sample relocation (SINIT's move_samples_to_sideways_ram_loop):
	//   [ ] Copy main-RAM $1984-$5883 (16128 bytes) into a sideways bank
	//       (e.g. bank 4 at offset $0100..$3FFF) so the sound-playback code
	//       at $99EC finds samples where it expects them ($8100-$BFFF of the
	//       sample bank). Deferred — sound is out of scope until display works.
}

void Exile::GenerateBackgroundGrid() {
	// Inject some dummy code
	BBC.ram[0xffa0] = 0x20; // JSR <grid_classify_routine>
	BBC.ram[0xffa1] = (uint8_t)(GAME_RAM_GRID_CLASSIFY & 0xFF);
	BBC.ram[0xffa2] = (uint8_t)(GAME_RAM_GRID_CLASSIFY >> 8);
	BBC.ram[0xffa3] = 0xa8; // TAY

	// Snapshot zero-page once so every iteration starts from a known clean state —
	// otherwise each classify's scratch writes accumulate and corrupt later iterations.
	uint8_t zpPerIter[256];
	for (int i = 0; i < 256; i++) zpPerIter[i] = BBC.ram[i];

	for (int y = 0; y < 256; y++) {
		for (int x = 0; x < 256; x++) {

			// Restore zero-page to pristine state before each classify call.
			for (int i = 0; i < 256; i++) BBC.ram[i] = zpPerIter[i];

			// Set stack pointer and PC:
			BBC.cpu.a = 0x00; BBC.cpu.x = 0x00; BBC.cpu.y = 0x00;
			BBC.cpu.stkp = 0xff;  BBC.cpu.pc = 0xffa0;

			// Set background square to check:
			BBC.ram[0x0095] = x; //square_x
			BBC.ram[0x0097] = y; //square_y

			// Run BBC to determine background tile and palette
			uint64_t nGuard = 0;
			bool bHung = false;
			do {
				do BBC.cpu.clock();
				while (!BBC.cpu.complete());
				if (++nGuard > 1000000ULL) {
					// Skip this tile — leave its TileGrid entry as it was before
					bHung = true;
					break;
				}
			} while (BBC.cpu.pc != 0xffa3);
			if (bHung) continue;

			TileGrid[x][y].TileID = BBC.ram[0x08]; // square_sprite
			TileGrid[x][y].Orientation = BBC.ram[0x09]; // square_orientation
			TileGrid[x][y].Palette = BBC.ram[0x73]; // this_object_palette

			TileGrid[x][y].GameX = (BBC.ram[0x4f] | (BBC.ram[0x53] << 8)) / 8; //(this_object_x_low + (this_object_x << 8)) / 16;
			TileGrid[x][y].GameY = (BBC.ram[0x51] | (BBC.ram[0x55] << 8)) / 8; //(this_object_y_low + (this_object_y << 8)) / 8;
			TileGrid[x][y].SpriteID = BBC.ram[0x75]; //this_object_sprite

			TileGrid[x][y].FrameLastDrawn = 0x10000; // ie, effectively never

			// Keep a separate record if it's a water tile and set the TileGrid to blank
			if (TileGrid[x][y].TileID == 0x0d) { // 0x0d = water tile
				XY WaterTile;
				WaterTile.GameX = x;
				WaterTile.GameY = y;
				WaterTiles.push_back(WaterTile);
				TileGrid[x][y].TileID = GAME_TILE_BLANK; // 0x19 = blank tile
			}
		}
		if ((y & 0x0f) == 0) { std::cout << "grid y=0x" << std::hex << y << std::dec << std::endl; std::cout.flush(); }
	}
	// Dump tile-ID histogram so we can see whether classify produced sensible output.
	int hist[256] = {0};
	for (int yy = 0; yy < 256; yy++) for (int xx = 0; xx < 256; xx++) hist[TileGrid[xx][yy].TileID]++;
	std::cout << "TileID histogram (id:count, only nonzero):" << std::endl;
	for (int i = 0; i < 256; i++) if (hist[i] > 0)
		std::cout << "  $" << std::hex << i << std::dec << " : " << hist[i] << std::endl;
	std::cout.flush();
}

void Exile::RestoreOldBytesInRange(uint16_t lo, uint16_t hi) {
	std::ifstream f("diff_old_bytes.txt");
	if (!f.is_open()) { std::cout << "diff_old_bytes.txt missing\n"; return; }
	std::string addrs, vals; int count = 0;
	while (f >> addrs >> vals) {
		uint32_t a = std::stoul(addrs, nullptr, 16);
		uint32_t v = std::stoul(vals, nullptr, 16);
		if (a >= lo && a < hi) { BBC.ram[a] = (uint8_t)v; count++; }
	}
	std::cout << "restored " << count << " bytes in [" << std::hex << lo << "," << hi << ")" << std::dec << std::endl;
}

void Exile::GenerateSpriteSheet() {
	int pixel_i = 0; int pixel_j = 0;
	for (int nRAM = SPRITE_BITMAP_BASE; nRAM < SPRITE_BITMAP_END; nRAM++) {
		uint8_t b = BBC.read((uint16_t)nRAM);  // go via Bus so sideways paging is honoured
		for (int k = 0; k < 4; k++) {
			int nColourIndex = 2 * ((b >> (7 - k)) & 1) + ((b >> (3 - k)) & 1);
			nSpriteSheet[pixel_i][pixel_j] = nColourIndex;
			pixel_i++;
		}

		if (pixel_i == 0x80) {
			pixel_i = 0;
			pixel_j++;
		}
	}
}

void Exile::DrawExileParticle(olc::PixelGameEngine* PGE, int32_t nScreenX, int32_t nScreenY, float fZoom, uint8_t nDoubleHeight, olc::Pixel p) {

	float fHorizontalZoom = fZoom;
	float fVerticalZoom = fZoom;

	float fWidth = fHorizontalZoom * 2.0f;
	float fHeight = fVerticalZoom;

	if (nDoubleHeight == 1) {
		nScreenX = nScreenX - fVerticalZoom;
		fHeight = fVerticalZoom * 2.0f;
	}

	PGE->FillRectDecal(olc::vf2d(nScreenX, nScreenY), olc::vf2d(fWidth, fHeight), p);
}

void Exile::DrawExileSprite_PixelByPixel(olc::PixelGameEngine* PGE, 
	                                     uint8_t nSpriteID, 
	                                     int32_t nX, int32_t nY,
	                                     uint8_t nPaletteID, 
	                                     uint8_t nHorizontalInvert, uint8_t nVerticalInvert,
	                                     uint8_t nTeleporting, uint8_t nTimer) {

	//Get sprite info from RAM (via Bus so paged sideways RAM works for enhanced):
	uint8_t nWidth   = BBC.read((uint16_t)(SPRITE_WIDTH_LOOKUP + nSpriteID));
	uint8_t nHeight  = BBC.read((uint16_t)(SPRITE_HEIGHT_LOOKUP + nSpriteID));
	uint8_t nOffsetA = BBC.read((uint16_t)(SPRITE_OFFSET_A_LOOKUP + nSpriteID));
	uint8_t nOffsetB = BBC.read((uint16_t)(SPRITE_OFFSET_B_LOOKUP + nSpriteID));

	//Sprites with bit 0 set in width / height, have "built in" flipping
	nHorizontalInvert = (nWidth & 0x01) ^ nHorizontalInvert;
	nVerticalInvert = (nHeight & 0x01) ^ nVerticalInvert;

	//Extract dimensions - Part 1
	nWidth = nWidth & 0xf0;
	nHeight = nHeight & 0xf8;

	//Adjust dimensions if teleporting:
	if (nTeleporting == 1) {
		uint8_t nRnd7 = (nTimer >> 1);
		nRnd7 = (nRnd7 >> 1) + (nRnd7 & 1);
		nRnd7 = (nRnd7 + nTimer) & 0x07;
		uint8_t n9c = nWidth;
		if (nRnd7 > 0) { for (uint8_t i = 0; i < nRnd7; i++) n9c = (n9c >> 1); }
		uint8_t nA = (((nWidth - n9c) & 0xff) >> 1) & 0xf0;
		nOffsetA = nOffsetA + nA;
		nX = nX + (((nA & 0xf0) >> 4) | ((nA & 0x0f) << 4));
		nWidth = n9c & 0xf0;

		nRnd7 = nTimer & 0x07;
		n9c = nHeight;
		if (nRnd7 > 0) { for (uint8_t i = 0; i < nRnd7; i++) n9c = (n9c >> 1); }
		nA = (((nHeight - n9c) & 0xff) >> 1) & 0xf8;
		nOffsetB = nOffsetB + nA;
		nY = nY + (((nA & 0xf8) >> 3) | ((nA & 0x07) << 5));
		nHeight = n9c & 0xf8;
	}

	//Extract dimensions - Part 2
	nWidth = (nWidth >> 4);
	nHeight = (nHeight >> 3);
	nOffsetA = ((nOffsetA & 0xf0) >> 4) | ((nOffsetA & 0x0f) << 4);
	nOffsetB = ((nOffsetB & 0xf8) >> 3) | ((nOffsetB & 0x07) << 5);

	//Extract palette:
	olc::Pixel Palette[4];
	uint8_t nPixelTableA = BBC.ram[PALETTE_PIXEL_TABLE  + (nPaletteID >> 4)];
	uint8_t nPixelTableB = BBC.ram[PALETTE_VALUE_LOOKUP + (nPaletteID & 0x0f)];
	for (int nCol = 0; nCol < 4; nCol++) {
		uint8_t byte = 0;
		switch (nCol) {
		case 1: byte = (nPixelTableB & 0x55) << 1; break;
		case 2: byte = nPixelTableB & 0xaa; break;
		case 3: byte = (nPixelTableA & 0x55) << 1; break;
		}
		Palette[nCol] = olc::Pixel(((byte >> 1) & 1) * 0xFF, ((byte >> 3) & 1) * 0xFF, ((byte >> 5) & 1) * 0xFF);
	}

	//Draw sprite:
	for (int j = 0; j < nHeight + 1; j++) {
		for (int i = 0; i < nWidth + 1; i++) {
			int nCol = SpriteSheet(nOffsetA + i, nOffsetB + j);
			if (nCol != 0) { // Col 0 => blank
				int nPixelX;  int nPixelY;
				if (nHorizontalInvert == 0) nPixelX = nX + i * 2; else nPixelX = nX + (nWidth - i) * 2;
				if (nVerticalInvert == 0) nPixelY = nY + j; else nPixelY = nY + nHeight - j;
				PGE->Draw(nPixelX, nPixelY, Palette[nCol]);
				PGE->Draw(nPixelX + 1, nPixelY, Palette[nCol]);
			}
		}
	}
}

void Exile::DrawExileSprite(olc::PixelGameEngine* PGE, 
	                        uint8_t nSpriteID,
	                        int32_t nScreenX, int32_t nScreenY, float fZoom,
	                        uint8_t nPaletteID, 
	                        uint8_t nHorizontalInvert, uint8_t nVerticalInvert,
	                        uint8_t nTeleporting, uint8_t nTimer) {
	
	uint32_t nSpriteKey;
	static olc::Sprite *sprSprite;
	static olc::Decal *decSprite;

	nSpriteKey = (nTeleporting << 24) | (nTimer << 16) | (nSpriteID << 8) | nPaletteID;

	//---------------------------------------------------------------------------------
	// Check cache
	//---------------------------------------------------------------------------------
	if (SpriteDecals.find(nSpriteKey) == SpriteDecals.end()) {
		// Draw sprite from scratch if the sprite key combination has not yet been cached

		// Save draw target:
		olc::Sprite* sprDrawTarget = PGE->GetDrawTarget();

		//Extract width and height (via Bus so paged sideways RAM works for enhanced):
		uint8_t nWidth  = BBC.read((uint16_t)(SPRITE_WIDTH_LOOKUP + nSpriteID));
		uint8_t nHeight = BBC.read((uint16_t)(SPRITE_HEIGHT_LOOKUP + nSpriteID));
		nWidth = (nWidth >> 4);
		nHeight = (nHeight >> 3);

		//Create and draw sprite:
		sprSprite = new olc::Sprite((nWidth + 1) * 2, (nHeight + 1));
		PGE->SetDrawTarget(sprSprite);
		PGE->Clear(olc::BLANK);

		DrawExileSprite_PixelByPixel(PGE, nSpriteID, 0, 0, nPaletteID, 0, 0, nTeleporting, nTimer);

		decSprite = new olc::Decal(sprSprite);
		SpriteDecals.insert(std::make_pair(nSpriteKey, decSprite));

		// Restore draw target:
		PGE->SetDrawTarget(sprDrawTarget);
	} 
	else {
		// Or restored cached sprite
		sprSprite = SpriteDecals[nSpriteKey]->sprite;
		decSprite = SpriteDecals[nSpriteKey];
	}
	//---------------------------------------------------------------------------------

	//---------------------------------------------------------------------------------
	// Draw sprite (decal)
	//---------------------------------------------------------------------------------
	// Apply horizontal and vertical flip
	float fHorizontalZoom = fZoom * 1.005; // 1.005 multiple seems to make the scrolling smoother?
	float fVerticalZoom = fZoom * 1.005; // 1.005 multiple seems to make the scrolling smoother?

	if (int(fHorizontalZoom) != fHorizontalZoom) fZoom * 1.005; // If non-integer, scale by an extra 1.005 to help avoid gaps in tiles;
	if (int(fVerticalZoom) != fVerticalZoom) fZoom * 1.005; // If non-integer, scale by an extra 1.005 to help avoid gaps in tiles;

	if (nHorizontalInvert == 1) {
		nScreenX = nScreenX + round(fHorizontalZoom * (sprSprite->width));
		fHorizontalZoom = -fHorizontalZoom;
	}
	if (nVerticalInvert == 1) {
		nScreenY = nScreenY + round(fVerticalZoom * (sprSprite->height));
		fVerticalZoom = -fVerticalZoom;
	}

	PGE->DrawDecal(olc::vf2d(nScreenX, nScreenY), decSprite, olc::vf2d(fHorizontalZoom, fVerticalZoom));
	//---------------------------------------------------------------------------------
}

void Exile::Initialise()
{
	GenerateSpriteSheet();

	// Snapshot & restore zero-page around the grid gen so the classify's scratch writes
	// don't corrupt the live game state carried over from the boot snapshot.
	uint8_t zpSave[256];
	for (int i = 0; i < 256; i++) zpSave[i] = BBC.ram[i];
	GenerateBackgroundGrid();
	for (int i = 0; i < 256; i++) BBC.ram[i] = zpSave[i];

	BBC.cpu.stkp = 0xff;
	BBC.cpu.pc = GAME_RAM_STARTGAMELOOP;
}

Obj Exile::Object(uint8_t nObjectID)
{
	Obj O;

	O.ObjectType = BBC.ram[OS_TYPE + nObjectID];
	O.SpriteID   = BBC.ram[OS_SPRITE + nObjectID];
	O.GameX = (BBC.ram[OS_X_LOW + nObjectID] | (BBC.ram[OS_X + nObjectID] << 8)) / 8;
	O.GameY = (BBC.ram[OS_Y_LOW + nObjectID] | (BBC.ram[OS_Y + nObjectID] << 8)) / 8;
	O.Palette = BBC.ram[OS_PALETTE + nObjectID];
	O.Timer   = BBC.ram[OS_TIMER + nObjectID];

	uint8_t nObjFlags = BBC.ram[OS_FLAGS + nObjectID];
	O.Teleporting = (nObjFlags >> 4) & 1; // Bit 4: Teleporting
	O.HorizontalFlip = (nObjFlags >> 7) & 1; // Bit 7: Horizontal invert
	O.VerticalFlip = (nObjFlags >> 6) & 1; // Bit 6: Vertical invert

	return O;
}

ExileParticle Exile::Particle(uint8_t nparticleID) {
	ExileParticle P;

	const uint16_t off = nparticleID * PS_STRIDE;
	P.GameX = (BBC.ram[PS_X_LOW + off] | (BBC.ram[PS_X + off] << 8)) / 8;
	P.GameY = (BBC.ram[PS_Y_LOW + off] | (BBC.ram[PS_Y + off] << 8)) / 8;
	P.ParticleType = BBC.ram[PS_TYPE + off];

	return P;
}

Tile Exile::BackgroundGrid(uint8_t x, uint8_t y)
{
	//To do: incorporate bound checking, and shift adjustments here?
	return TileGrid[x][y];
}

void Exile::DetermineBackground(uint8_t x, uint8_t y, uint16_t nFrameCounter) {

	//Clawed robots:
	if ((x == 0x75) && (y == 0x87)) return; // To prevent spawning magenta clawed robot too soon
	if ((x == 0x2e) && (y == 0xd6)) return; // To prevent spawning green clawed robot too soon
	// Note: cyan and red clawed robot behave well already

	//Birds:
	if ((x == 0xb0) && (y == 0x4e)) return;
	//if ((x == 0x77) && (y == 0x54)) return; // Tree
	//if ((x == 0x64) && (y == 0x80)) return; // Tree
	if ((x == 0x80) && (y == 0x88)) return;
	//if ((x == 0x62) && (y == 0x72)) return; // Tree
	if ((x == 0xe4) && (y == 0xb4)) return;
	//if ((x == 0x62) && (y == 0xa2)) return; // Tree
	//if ((x == 0x63) && (y == 0xb5)) return; // Tree
	if ((x == 0x47) && (y == 0x59)) return;
	if ((x == 0x84) && (y == 0x70)) return;
	if ((x == 0x9e) && (y == 0x69)) return;

	//Imps:
	//if ((x == 0x64) && (y == 0x94)) return; //Tree
	//if ((x == 0x47) && (y == 0xc0)) return; //Tree
	if ((x == 0x87) && (y == 0xbf)) return;

	//if ((x == 0x2b) && (y == 0x80)) return; //Tree
	if ((x == 0x80) && (y == 0x75)) return;
	//if ((x == 0x8a) && (y == 0x78)) return; //Tree

	//if ((x == 0xa7) && (y == 0x9a)) return; //Tree
	//if ((x == 0xc6) && (y == 0xbe)) return; //Tree
	//if ((x == 0x64) && (y == 0xc6)) return; //Tree

	if (TileGrid[x][y].FrameLastDrawn != (nFrameCounter - 1)) {
		// Save CPU state (NB: original code had a typo — y_ was set from BBC.cpu.x)
		uint8_t a_ = BBC.cpu.a; uint8_t x_ = BBC.cpu.x; uint8_t y_ = BBC.cpu.y;
		uint16_t stkp_ = BBC.cpu.stkp; uint16_t pc_ = BBC.cpu.pc;

		// Save zero-page: classify uses ZP as scratch — without the restore below,
		// every DetermineBackground call corrupts the game's live ZP state. This isn't
		// about TileGrid (we don't re-populate tiles here) — it's side-effect spawning,
		// so the game's own ZP must be unperturbed afterwards.
		uint8_t zp[256];
		for (int i = 0; i < 256; i++) zp[i] = BBC.ram[i];

		// Set CPU state:
		BBC.cpu.a = 0x00; BBC.cpu.x = 0x00; BBC.cpu.y = 0x00;
		BBC.cpu.stkp = 0xff;  BBC.cpu.pc = 0xffa0;

		// Set background square to check:
		BBC.ram[0x0095] = x; //square_x
		BBC.ram[0x0097] = y; //square_y

		// Run BBC to determine background tile and palette
		do {
			do BBC.cpu.clock();
			while (!BBC.cpu.complete());
		} while (BBC.cpu.pc != 0xffa3);

		// Restore ZP & CPU state:
		for (int i = 0; i < 256; i++) BBC.ram[i] = zp[i];
		BBC.cpu.a = a_; BBC.cpu.x = x_; BBC.cpu.y = y_;
		BBC.cpu.stkp = stkp_;  BBC.cpu.pc = pc_;
	}
	TileGrid[x][y].FrameLastDrawn = nFrameCounter;
}

uint8_t Exile::SpriteSheet(uint8_t i, uint8_t j)
{
	return nSpriteSheet[i][j];
}

uint16_t Exile::WaterLevel(uint8_t x) 
{
	uint16_t nWaterLevelHigh_1 = BBC.ram[0x0832 + 1]; // water_level[1]
	uint16_t nWaterLevelLow_1 = BBC.ram[0x082e + 1]; // water_level_low[1]
	uint16_t nWaterLevel_1 = ((nWaterLevelHigh_1 << 8) | nWaterLevelLow_1) / 8;

	for (int i = 3; i >= 0; i--) {

		uint8_t nRangeX = BBC.ram[GAME_RAM_X_RANGES + i]; // x_ranges

		if (x > nRangeX){
			uint16_t nWaterLevelHigh = BBC.ram[0x0832 + i]; // water_level[i]
			uint16_t nWaterLevelLow = BBC.ram[0x082e + i]; // water_level_low[i]
			uint16_t nWaterLevel = ((nWaterLevelHigh << 8) | nWaterLevelLow) / 8;
			if (nWaterLevel > nWaterLevel_1) nWaterLevel = nWaterLevel_1; // But no lower (ie Y can't be *higher*) than water range 1
			return nWaterLevel;
		}
	}
}

void Exile::Cheat_GetAllEquipment() 
{
	//Acquire all keys and equipment - optional! :)
	for (int i = 0; i < (0x0818 - 0x0806); i++) BBC.ram[0x0806 + i] = 0xff;
}

void Exile::Cheat_StoreAnyObject()
{
	// HD "bypass sprite-too-tall" BCS → CLC CLC. Address differs between variants
	// (std $34C6 · enh $352D), hence the variant-switched constant in Exile.h.
	BBC.ram[HD_SPRITE_TOO_TALL_BCS    ] = 0x18;
	BBC.ram[HD_SPRITE_TOO_TALL_BCS + 1] = 0x18;
}
