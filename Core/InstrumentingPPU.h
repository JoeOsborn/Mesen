#pragma once
#include "stdafx.h"
#include "PPU.h"
#include "BaseMapper.h"
#include "HdData.h"

struct InstPixelData {
  HdTileKey key;
  uint8_t XScroll;
  uint8_t YScroll;
};

struct InstSpriteData {
  HdTileKey key;
  uint8_t X;
  uint8_t Y;
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

  // Per-frame info
  HdPpuTileInfo tile, sprite;
  InstPixelData tileData[PPU::PixelCount];
  InstSpriteData spriteData[0x100];
};
