#include <cstdio>
#include <iostream>
#include <tuple>

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

template<typename T> void write_seq(std::ostream &strm, const T* data, size_t count) {
  strm.write((const char *)(data), sizeof(T)*count);
}

template<typename T> void write_obj(std::ostream &strm, T thing) {
  strm.write((const char *)(&thing), sizeof(T));
}

//TODO: reader functions like the two above?

void BlastOneTile(HdTileKey t, std::ostream &strm) {
  write_obj(strm, t.GetHashCode());
  write_obj(strm, t.TileIndex);
  write_obj(strm, t.PaletteColors);
  write_seq(strm, t.ToRgb().data(), 8*8);
}

//TODO: get some C++ mojo to combine this one with the next one
void BlastTiles(std::unordered_set<HdTileKey> coll, std::ostream &strm) {
  uint32_t count = coll.size();
  write_obj(strm, count);
  for(auto t = coll.begin(); t != coll.end(); t++) {
    //each one is 4+4+4+(8*8*4) = 268 bytes
    BlastOneTile(*t, strm);
  }
}

void BlastTiles(std::vector<HdTileKey> coll, std::ostream &strm) {
  uint32_t count = coll.size();
  write_obj(strm, count);
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
  write_obj(str, (uint8_t)0);
  str.flush();
}

int main(int argc, char**argv) {

  //std::setvbuf(stdout,NULL,_IONBF,1024*1024*8);
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
  DefaultVideoFilter filter;
  auto ippu = Console::Instrument();
  Console::Resume();

  //TODO: test everything from python!
  
  uint8_t cmd_buf[1024*1024];
  while(1) {
    std::cerr << "Send ready\n";
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
      std::cerr << "start send\n";
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
      numMovesLSB = cmd_buf[3];
      numMovesMSB = cmd_buf[4];
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

	  InstPixelData prev_pd = ippu->tileData[0];
	  uint32_t count = 1;

	  int datapoints = 0;
	  std::vector<std::tuple<uint32_t,InstPixelData> > pixels_data;
          for(int i = 1; i < PPU::PixelCount; i++) {
	    InstPixelData pd = ippu->tileData[i];
	    if (pd.key.GetHashCode() == prev_pd.key.GetHashCode() &&
		pd.XScroll == prev_pd.XScroll &&
		pd.YScroll == prev_pd.YScroll){
	      count += 1;
	    }
	    else {

	      datapoints += 1;

	      pixels_data.push_back(std::tuple<uint32_t, InstPixelData>(count,prev_pd));
	      
	      //std::cerr <<  "{" << prev_pd.key.GetHashCode() << "," <<  prev_pd.XScroll << "," << prev_pd.YScroll << "} = " << (uint32_t) count  <<"\n";
	      prev_pd = pd;
	      count = 1;
	    }
	    
	  }
      
	datapoints += 1;

	//std::cerr <<  "{" << prev_pd.key.GetHashCode() << "," <<  prev_pd.XScroll << "," << prev_pd.YScroll << "} = " << (uint32_t) count  <<"\n";
	      
	pixels_data.push_back(std::tuple<uint32_t, InstPixelData>(count,prev_pd));
	
	write_obj(std::cout, (uint32_t) pixels_data.size());
	for (int i = 0; i < pixels_data.size(); ++i){
	  write_obj(std::cout, std::get<0>(pixels_data[i]));
	  write_obj(std::cout,  std::get<1>(pixels_data[i]).key.GetHashCode());
	  write_obj(std::cout, std::get<1>(pixels_data[i]).XScroll);
	  write_obj(std::cout, std::get<1>(pixels_data[i]).YScroll);

	}
	
	//std::cerr << datapoints << " vs " <<PPU::PixelCount << " " <<PPU::PixelCount/datapoints  << " ENDTBP\n";
	/*
	for(int i = 0; i < PPU::PixelCount; i++) {
            InstPixelData pd = ippu->tileData[i];
            write_obj(std::cout, pd.key.GetHashCode());
            write_obj(std::cout, pd.XScroll);
            write_obj(std::cout, pd.YScroll);
          }
	*/
        }
	
        if(infos & LiveSprites) {
          //TODO: update all cout << junk to use write or put as needed
          //write sprite data, int32 hashkey + int8 + int8 + int8
          uint32_t scount = ippu->GetSpriteCount();
          //std::cerr << "Get sprite count " << scount << "\n";
          write_obj(std::cout, scount);
          for(int i = 0; i < ippu->spritesThisFrame; i++) {
            //each one is 4+1+1+1 = 7 bytes
            InstSpriteData pd = ippu->spriteData[i];
            if(pd.key.TileIndex == HdTileKey::NoTile) {
              continue;
            }
            write_obj(std::cout, pd.key.GetHashCode());
            write_obj<uint8_t>(std::cout,
                               (pd.key.HorizontalMirroring << 2) |
                               (pd.key.VerticalMirroring << 1) |
                               (pd.key.BackgroundPriority << 0));
            write_obj(std::cout, pd.X);
            write_obj(std::cout, pd.Y);
          }
          std::cerr << "sprites done\n";
        }
        //Flush after each step so the other side can read read read
        std::cout.flush();
        std::cerr.flush();
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
      write_obj(std::cout, (uint32_t) stateLen);
      //then write save state bytes
      saveState.seekg(0, ios::beg);
      // The one legit use we have for <<
      std::cout << saveState.rdbuf();
      //then reposition file stream to 0 again
      saveState.seekg(0, ios::beg);
      break;
    case LoadState:
      //LoadState, infos, statelen, statebuf
      read = std::fread(cmd_buf, sizeof(uint8_t), 1, stdin);
      if(read != 1) {
        std::cerr << "Couldn't read enough bytes in loadstate metadata A\n";
        abort();
      }
      infos = (InfoMask)cmd_buf[0];
      read = std::fread(cmd_buf, sizeof(uint32_t), 1, stdin);
      if(read != sizeof(uint32_t)) {
        std::cerr << "Couldn't read enough bytes in loadstate metadata B\n";
        abort();
      }
      stateLen = *((uint32_t*)(cmd_buf));
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
