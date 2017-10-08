#include <cstdio>
#include <iostream>

#ifdef _WIN32
# include <io.h>
# include <fcntl.h>
# define SET_BINARY_MODE(handle) setmode(handle, O_BINARY)
#else
# define SET_BINARY_MODE(handle) ((void)0)
#endif

#include <Core/EmulationSettings.h>
#include <Core/Console.h>
#include <Core/CPU.h>
#include <Core/InstrumentingPPU.h>
#include <Core/DefaultVideoFilter.h>
#include <Utilities/FolderUtilities.h>
#include <Core/SoundMixer.h>
#include <Core/IKeyManager.h>
#include <Core/ControlManager.h>
#include <Core/BaseControlDevice.h>

uint8_t *fb[PPU::OutputBufferSize];
stringstream saveState;

void SaveState() {
  saveState.seekg(0, ios::beg);
  Console::SaveState(saveState);
  saveState.seekg(0, ios::beg);
}


void RunOneFrame(uint8_t p1, uint8_t p2) {
  ControlManager::GetControlDevice(0)->OverrideState(p1);
  ControlManager::GetControlDevice(1)->OverrideState(p2);
  int curFrame = PPU::GetFrameCount();
  while(PPU::GetFrameCount() == curFrame) {
    Console::RunOneStep();
  }
  ControlManager::GetControlDevice(0)->OverrideClear();
  ControlManager::GetControlDevice(1)->OverrideClear();
}

void WritePPM(string outfile, size_t w, size_t h, size_t bpp, uint8_t*buf) {
  if(bpp < 3 || bpp > 4) { abort(); }
  size_t bufSize = w*h*bpp;
  ofstream imgOut(outfile, ios::binary | ios::out);
  imgOut << ("P6\n "+std::to_string(w)+" "+std::to_string(h)+"\n 255\n");
  for(int i = 0; i < bufSize; i += bpp) {
    imgOut.put(buf[i+2]);
    imgOut.put(buf[i+1]);
    imgOut.put(buf[i]);
  }
  imgOut.flush();
}

int main(int argc, char**argv) {
  //SET_BINARY_MODE(stdin);
  //SET_BINARY_MODE(stdout);
  FolderUtilities::SetHomeFolder("./");
  EmulationSettings::SetFlags(EmulationFlags::DisableGameDatabase);
  EmulationSettings::SetFlags(EmulationFlags::ForceMaxSpeed);
  EmulationSettings::SetControllerType(0, ControllerType::StandardController);
  EmulationSettings::SetControllerType(1, ControllerType::StandardController);
  Console::Pause();
  // TODO: ensure everything inside here is getting turned off correctly
  if(!Console::LoadROM(std::string(argv[1]))) {
    std::fwrite(">SROM not opened!\0", sizeof(uint8_t), 18, stdout);
    return -1;
  }
  auto filter = DefaultVideoFilter();
  auto ippu = Console::Instrument();
  Console::Resume();
  std::fwrite("<",sizeof(uint8_t),1,stdout);
  for(int i = 0; i < 60; i++) {
    RunOneFrame(i % 2 == 0 ? (1 << 3) : (1 << 0), 0);
  }
  SaveState();
  uint16_t *fb = (uint16_t*)calloc(sizeof(uint8_t), PPU::OutputBufferSize);
  ippu->CopyFrame((uint8_t*)fb);
  // Run it through a DefaultVideoFilter or NtscFilter
  filter.SendFrame(fb);
  // Dump it to file
  filter.TakeScreenshot(VideoFilterType::None, "out1.png", NULL);

  for(int i = 0; i < 240; i++) {
    RunOneFrame(1 << 7 | 1 << 1, 0);
  }
  
  ippu->CopyFrame((uint8_t*)fb);
  filter.SendFrame(fb);
  filter.TakeScreenshot(VideoFilterType::None, "out2.png", NULL);

  //TODO: dump out the instrumented tile data, sprite data, scrolling data.
  //this means: one png per realtile/a 256x240 matrix of tile id, scroll info per pixel/a list of sprites and positions and tile ids.
  //Two kinds of data:
  //agglomerative data like all witnessed tile and sprite keys
  //instantaneous data like tilemap, sprite data
  
  
  //Console::LoadState(state_buf, state_buf_length);

  saveState.seekg(0, ios::beg); 
  Console::LoadState(saveState);
  saveState.seekg(0, ios::beg);
  
  //SaveScreenshot()
  ippu->CopyFrame((uint8_t*)fb);
  filter.SendFrame(fb);
  FrameInfo fi = filter.GetFrameInfo();
  uint8_t *outputBuffer = filter.GetOutputBuffer();
  WritePPM("out4.ppm", fi.Width, fi.Height, fi.BitsPerPixel, outputBuffer);

  int firstThrough = ippu->allTiles.size();
  std::cout << "Seen tiles:" << firstThrough << "\n";
  for(int i = 0; i < 300; i++) {
    RunOneFrame(1 << 7 | 1 << 1 | (i%2 == 0 ? 1 : 0) << 0, 0);
  }
  ippu->CopyFrame((uint8_t*)fb);
  filter.SendFrame(fb);
  WritePPM("out5.ppm", fi.Width, fi.Height, fi.BitsPerPixel, filter.GetOutputBuffer());

  //outstream.write(outputBuffer, bufSize) to send the framebufer
  // it's a little redundant since the save state has it too?
  //outstream.write(state.rdBuf())
  for(auto t = ippu->allTiles.begin(); t != ippu->allTiles.end(); t++) {
    auto rgb = t->ToRgb();
    WritePPM("tiles/"+std::to_string(t->GetHashCode())+".ppm", 8, 8, 4, (uint8_t*)(rgb.data()));
  }
  for(auto t = ippu->allSpriteTiles.begin(); t != ippu->allSpriteTiles.end(); t++) {
    auto rgb = t->ToRgb();
    WritePPM("sprites/"+std::to_string(t->GetHashCode())+".ppm", 8, 8, 4, (uint8_t*)(rgb.data()));
  }

  //Later, throw this over the wall in a nice binary format
  for(int spr = 0; spr < 0x100; spr++) {
    InstSpriteData sd = ippu->spriteData[spr];
    if(sd.key.TileIndex == HdTileKey::NoTile) {
      continue;
    }
    std::cout << sd.key.TileIndex << " H:" << sd.key.GetHashCode() << " X:" << (int)sd.X << " Y:" << (int)sd.Y << "\n"; 
  }

  //Ditto this, but use the tileKey (or its hash?) instead of tidx.
  ofstream dump("screendump.txt");
  for(int j = 0; j < PPU::ScreenHeight; j++) {
    for(int i = 0; i < PPU::ScreenWidth; i++) {
      InstPixelData pd = ippu->tileData[j*PPU::ScreenWidth+i];
      auto tileKey = pd.key;
      int xsc = pd.XScroll;
      int ysc = pd.YScroll;
      uint32_t tidx = tileKey.TileIndex;
      dump.fill(' ');
      dump.width(3);
      dump << std::left << tidx << " " << xsc << " " << ysc << "    ";
    }
    dump << "\n";
  }
  dump.flush();
  
  Console::Halt();
  return 0;
}
