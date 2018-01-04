#include "InstrumentingPPU.h"
 
void InstrumentingPpu::DrawPixel()
{
  if(_scanline <= 0 && _cycle <= 1) {
    std::cerr << GetFrameCount() << " CLEAR\n";
    spritesThisFrame = 0;
  }
  if(IsRenderingEnabled() || ((_state.VideoRamAddr & 0x3F00) != 0x3F00)) {
			_lastSprite = nullptr;
			uint32_t color = GetPixelColor();
			_currentOutputBuffer[(_scanline << 8) + _cycle - 1] = _paletteRAM[color & 0x03 ? color : 0];
			uint32_t backgroundColor = 0;
			if(_flags.BackgroundEnabled && _cycle > _minimumDrawBgCycle) {
			 	backgroundColor = (((_state.LowBitShift << _state.XScroll) & 0x8000) >> 15) | (((_state.HighBitShift << _state.XScroll) & 0x8000) >> 14);
			}
			bool hasBgSprite = false;
			if(_lastSprite && _flags.SpritesEnabled) {
        if(backgroundColor == 0) {
				 	for(uint8_t i = 0; i < _spriteCount; i++) {
				 		if(_spriteTiles[i].BackgroundPriority) {
				 			hasBgSprite = true;
				 			break;
				 		}
				 	}
        }

				if(_lastSprite->AbsoluteTileAddr >= 0) {
					sprite.TileIndex = (_isChrRam ? (_lastSprite->TileAddr & _chrRamIndexMask) : _lastSprite->AbsoluteTileAddr) / 16;
					sprite.PaletteColors = ReadPaletteRAM(_lastSprite->PaletteOffset + 3) | (ReadPaletteRAM(_lastSprite->PaletteOffset + 2) << 8) | (ReadPaletteRAM(_lastSprite->PaletteOffset + 1) << 16) | 0xFF000000;
					sprite.IsChrRamTile = _isChrRam; 
					for(int i = 0; i < 16; i++) {
						sprite.TileData[i] = _mapper->GetMemoryValue(DebugMemoryType::ChrRom, _lastSprite->AbsoluteTileAddr / 16 * 16 + i);
					}
          uint8_t spriteY = _scanline - _lastSprite->OffsetY;
          //spriteData.insert({sprite, _lastSprite->SpriteX, spriteY});
          InstSpriteData thisSprite = {sprite, _lastSprite->SpriteX, spriteY};
          bool found = false;
          for(int i = 0; i < spritesThisFrame; i++) {
            if(spriteData[i] == thisSprite) {
              found = true;
              break;
            }
          }
          if(!found) {
            spriteData[spritesThisFrame] = thisSprite;
            spritesThisFrame++;
          }
					ProcessTile(_cycle - 1, _scanline, _lastSprite->AbsoluteTileAddr, sprite, _mapper, true, true);
				}
			}

			if(_flags.BackgroundEnabled) {
				TileInfo* lastTile = &((_state.XScroll + ((_cycle - 1) & 0x07) < 8) ? _previousTile : _currentTile);
				if(lastTile->AbsoluteTileAddr >= 0) {
					tile.TileIndex = (_isChrRam ? (lastTile->TileAddr & _chrRamIndexMask) : lastTile->AbsoluteTileAddr) / 16;
					tile.PaletteColors = ReadPaletteRAM(lastTile->PaletteOffset + 3) | (ReadPaletteRAM(lastTile->PaletteOffset + 2) << 8) | (ReadPaletteRAM(lastTile->PaletteOffset + 1) << 16) | (ReadPaletteRAM(0) << 24);
					tile.IsChrRamTile = _isChrRam;
          //TODO: might need to change for tall sprites?
					for(int i = 0; i < 16; i++) {
						tile.TileData[i] = _mapper->GetMemoryValue(DebugMemoryType::ChrRom, lastTile->AbsoluteTileAddr / 16 * 16 + i);
					}
					ProcessTile(_cycle - 1, _scanline, lastTile->AbsoluteTileAddr, tile, _mapper, false, hasBgSprite);
				}
			}
		} else {
			//"If the current VRAM address points in the range $3F00-$3FFF during forced blanking, the color indicated by this palette location will be shown on screen instead of the backdrop color."
			_currentOutputBuffer[(_scanline << 8) + _cycle - 1] = _paletteRAM[_state.VideoRamAddr & 0x1F];
		}
	}


void InstrumentingPpu::ProcessTile(uint32_t x, uint32_t y, uint16_t tileAddr, HdPpuTileInfo &tile, BaseMapper *mapper, bool isSprite, bool transparencyRequired)
  {
    HdTileKey dummy;
    dummy.TileIndex = HdTileKey::NoTile;
    tileData[y*PPU::ScreenWidth+x] = {dummy, 0, 0};
    if(tile.HorizontalMirroring) {
      //tiledata is 16 bytes, 2 per row.
      //to horizontally mirror, we flip each 2-bit pair in each byte and flip the two bytes in each row
      for(int i = 0; i < 16; i+=2) {
        uint8_t b1 = tile.TileData[i], b2 = tile.TileData[i+1];
        // 01 -> 67, 23 -> 45, 45 -> 23, 67 -> 01
        uint8_t b11 = (b1 >> 0) & 0x03;
        uint8_t b12 = (b1 >> 2) & 0x03;
        uint8_t b13 = (b1 >> 4) & 0x03;
        uint8_t b14 = (b1 >> 6) & 0x03;
        uint8_t b21 = (b2 >> 0) & 0x03;
        uint8_t b22 = (b2 >> 2) & 0x03;
        uint8_t b23 = (b2 >> 4) & 0x03;
        uint8_t b24 = (b2 >> 6) & 0x03;
        tile.TileData[i] = (b24 << 0) | (b23 << 2) | (b22 << 4) | (b21 << 6);
        tile.TileData[i+1] = (b14 << 0) | (b13 << 2) | (b12 << 4) | (b11 << 6);
      }
    }
    if(tile.VerticalMirroring) {
      //to vertically mirror, we swap whole rows
      for(int i = 0; i < 4; i++) {
        uint8_t rowA = i;
        uint8_t rowB = 8-1-i;
        uint8_t offA = rowA*2;
        uint8_t offB = rowB*2;
        uint8_t temp1 = tile.TileData[offA];
        uint8_t temp2 = tile.TileData[offA+1];
        tile.TileData[offA] = tile.TileData[offB];
        tile.TileData[offA+1] = tile.TileData[offB+1];
        tile.TileData[offB] = temp1;
        tile.TileData[offB+1] = temp2;
      }
    }

    if(!isSprite) {
      tileData[y*PPU::ScreenWidth+x] = {
        tile,
        _state.XScroll,
        tile.OffsetY
      };
    }

    OverscanDimensions overscan = EmulationSettings::GetOverscanDimensions();
    if(x < overscan.Left || y < overscan.Top || (PPU::ScreenWidth - x - 1) < overscan.Right || (PPU::ScreenHeight - y - 1) < overscan.Bottom) {
      //Ignore tiles inside overscan
      return;
    }

    if(isSprite) {
      auto findResult = allSpriteTiles.find(tile);
      if(findResult == allSpriteTiles.end()) {
        newSpriteTiles.push_back(tile);
        allSpriteTiles.insert(findResult, tile);
      }
    } else {
      auto findResult = allTiles.find(tile);
      if(findResult == allTiles.end()) {
        newTiles.push_back(tile);
        allTiles.insert(findResult, tile);
      }
    }
  }

void InstrumentingPpu::ResetNewTiles() {
  newTiles.clear();
}

void InstrumentingPpu::ResetNewSpriteTiles() {
  newSpriteTiles.clear();
}
