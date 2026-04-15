#pragma once
#include "olcPixelGameEngine.h"
#include "olc6502.h"
#include "Bus.h"

#include <array>
#include <cstdint>
#include <string>

const int GAME_TILE_WIDTH = 32;
const int GAME_TILE_HEIGHT = 32;
const uint8_t GAME_TILE_BLANK = 0x19;

#ifdef EXILE_VARIANT_SIDEWAYS_RAM
// BBC Master sideways-RAM (enhanced) ROM addresses — derived from exile-enhanced.txt.
const uint16_t GAME_RAM_INPUTS             = 0x1263;
const uint16_t GAME_RAM_PLAYER_TELEPORTING = 0x19d9;
const uint16_t GAME_RAM_STARTGAMELOOP      = 0x19da;
const uint16_t GAME_RAM_SCREENFLASH        = 0x1fd9;
const uint16_t GAME_RAM_EARTHQUAKE         = 0x2639;
const uint16_t GAME_RAM_GRID_CLASSIFY      = 0x23cb; // get_tile_and_set_sprite_variables
// Object/particle stack base addresses — sideways skips PatchExileRAM, so objects are at
// their ORIGINAL (un-relocated) addresses. Each base contains 16 entries; the HD
// 128-object expansion isn't applied for sideways yet.
const uint16_t OS_TYPE     = 0x0860;
const uint16_t OS_SPRITE   = 0x0870;
const uint16_t OS_X_LOW    = 0x0880;
const uint16_t OS_X        = 0x0891;
const uint16_t OS_Y_LOW    = 0x08a3;
const uint16_t OS_Y        = 0x08b4;
const uint16_t OS_FLAGS    = 0x08c6;
const uint16_t OS_PALETTE  = 0x08d6;
const uint16_t OS_TIMER    = 0x0956;
// Original (un-relocated) particle stack: interleaved 8 bytes per particle starting at $2907.
//   $2907,X = x_fraction   $2908,X = y_fraction   $2909,X = x   $290A,X = y
//   $290B,X = ttl          $290C,X = colour_and_flags
//   To get particle N's field, use BBC.ram[base + N * PS_STRIDE].
const uint8_t  PS_STRIDE   = 8;
const uint16_t PS_X_LOW    = 0x2907;   // x_fraction
const uint16_t PS_X        = 0x2909;
const uint16_t PS_Y_LOW    = 0x2908;   // y_fraction
const uint16_t PS_Y        = 0x290a;
const uint16_t PS_TYPE     = 0x290c;   // colour_and_flags (no separate "type" byte in original)
const uint16_t GAME_RAM_PARTICLE_COUNT = 0x1e8b;
#else
// BBC Micro standard ROM addresses.
const uint16_t GAME_RAM_INPUTS             = 0x126b;
const uint16_t GAME_RAM_PLAYER_TELEPORTING = 0x19b5;
const uint16_t GAME_RAM_STARTGAMELOOP      = 0x19b6;
const uint16_t GAME_RAM_SCREENFLASH        = 0x1fa6;
const uint16_t GAME_RAM_EARTHQUAKE         = 0x260a;
const uint16_t GAME_RAM_GRID_CLASSIFY      = 0x2398;
// Object/particle stack base addresses — standard HD relocates these via PatchExileRAM.
const uint16_t OS_TYPE     = 0x9600;
const uint16_t OS_SPRITE   = 0x9700;
const uint16_t OS_X_LOW    = 0x9800;
const uint16_t OS_X        = 0x9900;
const uint16_t OS_Y_LOW    = 0x9a00;
const uint16_t OS_Y        = 0x9b00;
const uint16_t OS_FLAGS    = 0x9c00;
const uint16_t OS_PALETTE  = 0x9d00;
const uint16_t OS_TIMER    = 0xa500;
const uint8_t  PS_STRIDE   = 1;
const uint16_t PS_X_LOW    = 0x8800;
const uint16_t PS_X        = 0x8a00;
const uint16_t PS_Y_LOW    = 0x8900;
const uint16_t PS_Y        = 0x8b00;
const uint16_t PS_TYPE     = 0x8d00;
const uint16_t GAME_RAM_PARTICLE_COUNT = 0x1e58;   // PatchExileRAM relocates the *8 form here
#endif

struct XY {
	uint16_t GameX;          uint16_t GameY;
};

struct Obj {
	uint8_t ObjectType;
	uint8_t SpriteID;        uint8_t Palette;
	uint8_t HorizontalFlip;  uint8_t VerticalFlip;  
	uint8_t Teleporting;     uint8_t Timer;
	uint16_t GameX;          uint16_t GameY;
};

struct Tile {
	uint8_t TileID;          uint8_t SpriteID;
	uint8_t Orientation;     uint8_t Palette;
	uint16_t GameX;          uint16_t GameY;
	uint32_t FrameLastDrawn;
};

struct ExileParticle {
	uint8_t ParticleType;    
	uint16_t GameX;          uint16_t GameY;
};

class Exile
{

private:
	bool ParseAssemblyLine(std::string sLine);
	void GenerateBackgroundGrid();
	void GenerateSpriteSheet();

	void CopyRAM(uint16_t nSource, uint16_t nTarget, uint8_t nLength);

	Tile TileGrid[256][256];
	uint8_t nSpriteSheet[128][128];

	std::map<uint32_t, olc::Decal*> SpriteDecals;

	void DrawExileSprite_PixelByPixel(olc::PixelGameEngine* PGE, uint8_t nSpriteID, int32_t nX, int32_t nY, uint8_t nPaletteID, uint8_t nHorizontalInvert = 0, uint8_t nVerticalInvert = 0, uint8_t nTeleporting = 0, uint8_t nTimer = 0);

public:
	Bus BBC;
	olc6502 cpu;

	void Initialise();
	void RestoreOldBytesInRange(uint16_t lo, uint16_t hi);
	bool bFirstWriteWins = false;
	bool LoadExileFromBinary(std::string sFile, uint16_t loadAddr);
	bool LoadExileFromBinaryToBank(std::string sFile, uint8_t bank, uint16_t offsetInBank);
	bool LoadExileFromDisassembly(std::string sFile);
	void PatchExileRAM();

	std::vector<XY> WaterTiles;

	Obj Object(uint8_t nObjectID);
	Tile BackgroundGrid(uint8_t x, uint8_t y);
	ExileParticle Particle(uint8_t nparticleID);

	void DetermineBackground(uint8_t x, uint8_t y, uint16_t nFrameCounter);

	uint8_t SpriteSheet(uint8_t i, uint8_t j);
	uint16_t WaterLevel(uint8_t x);

	void DrawExileParticle(olc::PixelGameEngine* PGE, int32_t nScreenX, int32_t nScreenY, float fZoom, uint8_t nDoubleHeight, olc::Pixel p);
	void DrawExileSprite(olc::PixelGameEngine* PGE, uint8_t nSpriteID, int32_t nScreenX, int32_t nScreenY, float fZoom, uint8_t nPaletteID, uint8_t nHorizontalInvert = 0, uint8_t nVerticalInvert = 0, uint8_t nTeleporting = 0, uint8_t nTimer = 0);

	void Cheat_GetAllEquipment();
	void Cheat_StoreAnyObject();
};
