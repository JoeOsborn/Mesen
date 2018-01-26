#pragma once
#include "stdafx.h"
#include "PPU.h"
#include "BaseMapper.h"
#include "HdData.h"
#include <set>

struct InstSpriteData
{
  // Intentionally slicing off offsetx, offsety, etc.
  HdPpuTileInfo key;
  uint8_t X;
  uint8_t Y;

  uint32_t GetHashCode() const
	{
    return key.GetHashCode() ^ X << 8 ^ Y;
	}

	bool operator==(const InstSpriteData &other) const
	{
		return key == other.key && X == other.X && Y == other.Y && key.HorizontalMirroring == other.key.HorizontalMirroring && key.VerticalMirroring == other.key.VerticalMirroring && key.BackgroundPriority == other.key.BackgroundPriority;
	}
};

namespace std {
	template <> struct hash<InstSpriteData>
	{
		size_t operator()(const InstSpriteData& x) const
		{
			return x.GetHashCode();
		}
	};
}


struct InstPixelData {
  HdTileKey key;
  uint8_t XScroll;
  uint8_t YScroll;
};

class InstrumentingPpu : public PPU
{
private:
  // Settings
	bool _isChrRam;
  //size_t _chrRamBankSize = 4*0x400;
  size_t _chrRamIndexMask = 4*0x400-1;

protected:
  void DrawPixel();
  void ProcessTile(uint32_t x, uint32_t y, uint16_t tileAddr, HdPpuTileInfo &tile, BaseMapper *mapper, bool isSprite, bool transparencyRequired);

public:
  InstrumentingPpu(BaseMapper* mapper) : PPU(mapper)
	{
		_isChrRam = !_mapper->HasChrRom();
	}

	void SendFrame()
	{
    UpdateGrayscaleAndIntensifyBits();
    _currentOutputBuffer = (_currentOutputBuffer == _outputBuffers[0]) ? _outputBuffers[1] : _outputBuffers[0];
    _enableOamDecay = EmulationSettings::CheckFlag(EmulationFlags::EnableOamDecay);
	}

  void CopyFrame(uint8_t *fb) {
    memcpy(fb, _currentOutputBuffer, PPU::OutputBufferSize*sizeof(uint8_t));
  }

  void BeginVBlank()
  {
    SendFrame();
    TriggerNmi();
  }

  // Aggregate info
  std::unordered_set<HdTileKey> allTiles;
  std::unordered_set<HdTileKey> allSpriteTiles;
  std::vector<HdTileKey> newTiles;
  std::vector<HdTileKey> newSpriteTiles;
  void ResetNewTiles();
  void ResetNewSpriteTiles();

  // Per-frame info
  HdPpuTileInfo tile, sprite;
  InstPixelData tileData[PPU::PixelCount];
  static const size_t MaxSpritesPerFrame = 1024;
  InstSpriteData spriteData[MaxSpritesPerFrame];
  int spritesThisFrame;

  size_t GetSpriteCount() {
    std::cerr << "SDS " << spritesThisFrame << "\n";
    return spritesThisFrame;
  }
};
