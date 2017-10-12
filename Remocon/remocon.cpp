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
stringstream saveState(stringstream::binary | stringstream::in | stringstream::out);

void SaveState() {
  saveState.seekg(0, ios::beg);
  Console::SaveState(saveState);
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

int main_test(int argc, char**argv) {
  FolderUtilities::SetHomeFolder("./");
  EmulationSettings::SetFlags(EmulationFlags::DisableGameDatabase);
  EmulationSettings::SetFlags(EmulationFlags::ForceMaxSpeed);
  EmulationSettings::SetControllerType(0, ControllerType::StandardController);
  EmulationSettings::SetControllerType(1, ControllerType::StandardController);
  Console::Pause();
  if(argc < 2) {
    std::cerr << "Not enough arguments, please include a ROM file path!\n";
    abort();
  }
  if(!Console::LoadROM(std::string(argv[1]))) {
    std::cerr << std::string(argv[1])+" SROM not opened!\n";
    abort();
  }
  auto filter(DefaultVideoFilter());
  auto ippu = Console::Instrument();
  Console::Resume();
  for(int i = 0; i < 60; i++) {
    RunOneFrame(i % 2 == 0 ? (1 << 3) : (1 << 0), 0);
  }
  SaveState();
  ippu->CopyFrame((uint8_t*)fb);
  // Run it through a DefaultVideoFilter or NtscFilter
  filter.SendFrame((uint16_t*)fb);
  // Dump it to file
  filter.TakeScreenshot(VideoFilterType::None, "out1.png", NULL);

  for(int i = 0; i < 240; i++) {
    RunOneFrame(1 << 7 | 1 << 1, 0);
  }
  
  ippu->CopyFrame((uint8_t*)fb);
  filter.SendFrame((uint16_t*)fb);
  filter.TakeScreenshot(VideoFilterType::None, "out2.png", NULL);

  //Two kinds of data:
  //agglomerative data like all witnessed tile and sprite keys
  //instantaneous data like tilemap, sprite data
  
  
  //Console::LoadState(state_buf, state_buf_length);

  saveState.seekg(0, ios::beg); 
  Console::LoadState(saveState);
  saveState.seekg(0, ios::beg);
  
  //SaveScreenshot()
  ippu->CopyFrame((uint8_t*)fb);
  filter.SendFrame((uint16_t*)fb);
  FrameInfo fi = filter.GetFrameInfo();
  uint8_t *outputBuffer = filter.GetOutputBuffer();
  WritePPM("out4.ppm", fi.Width, fi.Height, fi.BitsPerPixel, outputBuffer);

  int firstThrough = ippu->allTiles.size();
  std::cout << "Seen tiles:" << firstThrough << "\n";
  for(int i = 0; i < 300; i++) {
    RunOneFrame(1 << 7 | 1 << 1 | (i%2 == 0 ? 1 : 0) << 0, 0);
  }
  ippu->CopyFrame((uint8_t*)fb);
  filter.SendFrame((uint16_t*)fb);
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

enum InfoMask {
  None=0,
  FB=1<<0,
  NewTiles=1<<1,
  NewSpriteTiles=1<<2,
  TilesByPixel=1<<3,
  LiveSprites=1<<4,
  ReservedA=1<<5,
  ReservedB=1<<6,
  ReservedC=1<<7
};
// "Framebuffers" are always 256*240*4 bytes
// "NewTiles, NewSpriteTiles, TilesSoFar, SpriteTilesSoFar" are a length L, then L 268 byte sequences (4+4+4+(8*8*4))
// "TilesByPixel" is 256*240 units of int32 hash x Xscroll x Yscroll
// "LiveSprites" is a count C followed by C 6-byte sequences (int32 hash x Xpos x Ypos)

enum CtrlCommand {
  Step=0, //Infos NumPlayers BytesPerPlayer NumMovesMSB NumMovesLSB
          // -> steps emu. One FB, NewTiles, NewSpriteTiles per move; one TilesByPixel, LiveSprites at end of sequence 
  GetState=1, // -> state length, state
  LoadState=2, //LoadState, infos, statelen, statebuf
               // -> loads state; if infos & FB, sends framebuffer
  GetTilesSoFar=3,
  GetSpriteTilesSoFar=4
};

void BlastOneTile(HdTileKey t, std::ostream &strm) {
  strm << t.GetHashCode() << t.TileIndex << t.PaletteColors;
  strm.write((const char *)(t.ToRgb().data()), sizeof(uint32_t)*8*8);
}

//TODO: get some C++ mojo to combine this one with the next one
void BlastTiles(std::unordered_set<HdTileKey> coll, std::ostream &strm) {
  size_t count = coll.size();
  strm << count;
  for(auto t = coll.begin(); t != coll.end(); t++) {
    //each one is 4+4+4+(8*8*4) = 268 bytes
    BlastOneTile(*t, strm);
  }
}

void BlastTiles(std::vector<HdTileKey> coll, std::ostream &strm) {
  size_t count = coll.size();
  strm << count;
  for(auto t = coll.begin(); t != coll.end(); t++) {
    //each one is 4+4+4+(8*8*4) = 268 bytes
    BlastOneTile(*t, strm);
  }
}

void SendFramebuffer(std::shared_ptr<InstrumentingPpu> ippu, DefaultVideoFilter &filter, std::ostream &stream) {
  ippu->CopyFrame((uint8_t*)fb);
  filter.SendFrame((uint16_t*)fb);
  FrameInfo fi = filter.GetFrameInfo();
  uint8_t *outputBuffer = filter.GetOutputBuffer();
  size_t bufSize = fi.Width*fi.Height*fi.BitsPerPixel;
  stream.write((const char *)outputBuffer, sizeof(uint8_t)*bufSize);
  //TODO: check stream error flags??
}

void SendReady(std::ostream &str) {
  str << (uint8_t)0;
  str.flush();
}

int main(int argc, char**argv) {
  SET_BINARY_MODE(stdin);
  SET_BINARY_MODE(stdout);
  FolderUtilities::SetHomeFolder("./");
  EmulationSettings::SetFlags(EmulationFlags::DisableGameDatabase);
  EmulationSettings::SetFlags(EmulationFlags::ForceMaxSpeed);
  EmulationSettings::SetControllerType(0, ControllerType::StandardController);
  EmulationSettings::SetControllerType(1, ControllerType::StandardController);
  Console::Pause();
  if(argc < 2) {
    std::cerr << "Not enough arguments, please include a ROM file path!\n";
    abort();
  }
  if(!Console::LoadROM(std::string(argv[1]))) {
    std::cerr << std::string(argv[1])+" SROM not opened!\n";
    abort();
  }
  auto filter = DefaultVideoFilter();
  auto ippu = Console::Instrument();
  Console::Resume();

  //TODO: test everything from python!
  
  uint8_t cmd_buf[1024*1024];
  while(1) {
    SendReady(std::cout);
    size_t read = std::fread(cmd_buf, sizeof(uint8_t), 1, stdin);
    if(read == 0) { break; }
    size_t stateLen;
    InfoMask infos=None;
    uint16_t numMoves=0;
    uint8_t numMovesLSB=0, numMovesMSB=0, bytesPerPlayer=0, numPlayers=0;
    CtrlCommand cmd = (CtrlCommand)cmd_buf[0];
    switch(cmd) {
    case Step:
      //get the next parts of the command: 
      read = std::fread(cmd_buf, sizeof(uint8_t), 5, stdin);
      if(read != 5) {
        std::cerr << "Wrong number of bytes in Step command!\n";
        abort();
      }
      infos = (InfoMask)cmd_buf[0];
      numPlayers = cmd_buf[1];
      if(numPlayers > 4) {
        std::cerr << "NES is only up to four players!\n";
        abort();
      }
      bytesPerPlayer = cmd_buf[2];
      if(bytesPerPlayer != 1) {
        std::cerr << "NES is only one byte per player!\n";
        abort();
      }
      numMovesMSB = cmd_buf[3];
      numMovesLSB = cmd_buf[4];
      numMoves = ((uint16_t)numMovesMSB << 8) | (uint16_t)numMovesLSB;
      if(numMoves == 0) {
        std::cerr << "Got to give at least one move!\n";
        abort();
      }
      if((int32_t)(numMoves*bytesPerPlayer*numPlayers) > 65536) {
        std::cerr << "Too many moves, sorry!\n";
      }
      //We ought to read everything off the pipe before we can start sending our stuff.  Right??
      read = std::fread(cmd_buf, bytesPerPlayer, numMoves*numPlayers, stdin);
      if(read != numPlayers*numMoves) {
        std::cerr << "Ran out of bytes too early!\n";
        abort();
      }
      ippu->ResetNewTiles();
      ippu->ResetNewSpriteTiles();
      for(int i = 0; i < numMoves*numPlayers; i+=numPlayers) {
        uint8_t p1Move = cmd_buf[i];
        uint8_t p2Move = numPlayers == 2 ? cmd_buf[i+1] : 0;
        RunOneFrame(p1Move, p2Move);
        //once per frame
        if(infos & FB) {
          SendFramebuffer(ippu, filter, std::cout);
        }
        if(infos & TilesByPixel) {
          //write tiles-by-pixel thing, int32 hashkey + int8 + int8 = 6 bytes
          for(int i = 0; i < PPU::PixelCount; i++) {
            InstPixelData pd = ippu->tileData[i];
            std::cout << (int32_t)pd.key.GetHashCode();
            std::cout << (uint8_t)pd.XScroll << (uint8_t)pd.YScroll;
          }
        }
        if(infos & LiveSprites) {
          //write sprite data, int32 hashkey + int8 + int8
          size_t scount = ippu->GetSpriteCount();
          std::cout << (size_t)scount;
          for(int i = 0; i < scount; i++) {
            //each one is 4+1+1 = 6 bytes
            InstSpriteData pd = ippu->spriteData[i];
            if(pd.key.TileIndex == HdTileKey::NoTile) {
              continue;
            }
            std::cout << (int32_t)pd.key.GetHashCode() << (uint8_t)pd.X << (uint8_t)pd.Y;
          }
        }
        //Flush after each step so the other side can read read read
        std::cout.flush();
      }
      //once per Step call
      if(infos & NewTiles) {
        //blast ippu->newTiles
        BlastTiles(ippu->newTiles, std::cout);
      }
      if(infos & NewSpriteTiles) {
        //blast ippu->newSpriteTiles
        BlastTiles(ippu->newSpriteTiles, std::cout);
      }
      break;
    case GetState:
      //GetState
      SaveState();
      //get file stream position
      stateLen = saveState.tellp();
      //write that to stdout
      std::cout << (size_t)stateLen;
      //then write save state bytes
      saveState.seekg(0, ios::beg);
      std::cout << saveState.rdbuf();
      //then reposition file stream to 0 again
      saveState.seekg(0, ios::beg);
      break;
    case LoadState:
      //LoadState, infos, statelen, statebuf
      //TODO: load up and return framebuffer if infos & framebuffer
      read = std::fread(cmd_buf, sizeof(uint8_t), 1, stdin);
      if(read != 1) {
        std::cerr << "Couldn't read enough bytes in loadstate metadata A\n";
        abort();
      }
      infos = (InfoMask)cmd_buf[0];
      read = std::fread(cmd_buf, sizeof(size_t), 1, stdin);
      if(read != 1) {
        std::cerr << "Couldn't read enough bytes in loadstate metadata B\n";
        abort();
      }
      stateLen = cmd_buf[0];
      read = std::fread(cmd_buf, sizeof(uint8_t), stateLen, stdin);
      if(read != stateLen) {
        std::cerr << "Couldn't read enough bytes in loadstate payload\n";
        abort();
      }
      Console::LoadState(cmd_buf, stateLen);
      if(infos & FB) {
        SendFramebuffer(ippu, filter, std::cout);
      }
      break;
    case GetTilesSoFar:
      //GetTilesSoFar
      BlastTiles(ippu->allTiles, std::cout);
      break;
    case GetSpriteTilesSoFar:
      //GetSpriteTilesSoFar
      BlastTiles(ippu->allSpriteTiles, std::cout);
      break;
    default:
      break;
    }
  }
  Console::Halt();
  return 0;
}
