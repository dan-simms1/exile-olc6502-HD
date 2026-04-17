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

// BBC Micro standard ROM addresses (bmain.rom). These are used by the HD renderer
// (--hd mode), which only runs on the standard ROM. For --enhanced mode the runtime
// address overrides in Main.cpp (gStartGameLoop, gSampleTrapScream, etc.) replace
// the relevant subset.
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
const uint16_t SPRITE_BITMAP_BASE = 0x53EC;
const uint16_t SPRITE_BITMAP_END  = 0x5E0C;
const uint16_t SPRITE_WIDTH_LOOKUP    = 0x5E0C;
const uint16_t SPRITE_HEIGHT_LOOKUP   = 0x5E89;
const uint16_t SPRITE_OFFSET_A_LOOKUP = 0x5F06;
const uint16_t SPRITE_OFFSET_B_LOOKUP = 0x5F83;
const uint16_t PALETTE_PIXEL_TABLE    = 0x1E48;
const uint16_t PALETTE_VALUE_LOOKUP   = 0x0B79;
const uint16_t GAME_RAM_X_RANGES      = 0x14D2;
const uint16_t HD_SPRITE_TOO_TALL_BCS = 0x34C6;
const int      OBJECT_SLOTS           = 128;      // HD relocates to $9600+ with 128-slot stacks
// Sample-trigger PC traps for the standard ROM. play_sample doesn't exist
// in standard, so these PCs trigger our C++ SampleManager.Play() instead
// (the 6502's underlying play_sound BBC sound calls remain silent until
// we add an SN76489 emulator).
const uint16_t SAMPLE_TRAP_SCREAM          = 0x2497; // scream → samples 1-4 (Ow!/Ow/Ooh/Oooh!)
const uint16_t SAMPLE_TRAP_SCREAM_SKIP_TO  = 0x24A5; // RTS at end of scream (bypass both JSR $13FA calls)
const uint16_t SAMPLE_TRAP_HOVERING_ROBOT  = 0x480E; // hovering robot 1-in-256 sound → sample 6 (Radio die)
const uint16_t SAMPLE_TRAP_HR_SKIP_TO      = 0x4815; // skip past the JSR $13FA for hovering robot
const uint16_t SAMPLE_TRAP_CLAWED_ROBOT    = 0x4858; // clawed robot teleport → sample 5 (Destroy!)
const uint16_t SAMPLE_TRAP_CR_SKIP_TO      = 0x485F; // skip past the JSR $13FA for clawed teleport
// play_sound entry — same address in both variants.
const uint16_t SOUND_TRAP_PLAY_SOUND       = 0x13FA;

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

	void Initialise(bool bFaithful = false);  // bFaithful=true skips slow grid generation (native modes)
	void RestoreOldBytesInRange(uint16_t lo, uint16_t hi);
	bool bFirstWriteWins = false;
	bool LoadExileFromBinary(std::string sFile, uint16_t loadAddr);
	bool LoadExileFromBinaryToBank(std::string sFile, uint8_t bank, uint16_t offsetInBank);
	bool LoadExileFromDisassembly(std::string sFile);
	void PatchExileRAM();
	void PatchEnhancedExileRAM();   // Enhanced/sideways-ROM peer of PatchExileRAM

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
