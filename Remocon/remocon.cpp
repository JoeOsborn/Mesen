#include <cstdio>
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

void RunOneFrame(int p1btns, int p2btns) {
  int curFrame = PPU::GetFrameCount();
  while (PPU::GetFrameCount() == curFrame) {
    Console::RunOneStep();
  }
}

int main(int argc, char**argv) {
  //SET_BINARY_MODE(stdin);
  //SET_BINARY_MODE(stdout);
  FolderUtilities::SetHomeFolder("./");
  EmulationSettings::SetFlags(EmulationFlags::DisableGameDatabase);
  EmulationSettings::SetFlags(EmulationFlags::ForceMaxSpeed);
  Console::Pause();
  if(!Console::LoadROM(std::string(argv[1]))) {
    std::fwrite(">SROM not opened!\0", sizeof(uint8_t), 18, stdout);
    return -1;
  }
  auto filter = DefaultVideoFilter();
  {
      auto ippu = Console::Instrument();
      Console::Resume();
      std::fwrite("<",sizeof(uint8_t),1,stdout);
      int start_frame = -1;
      for(int i = 0; i < 120; i++) {
        RunOneFrame(0,0);
      }
      uint16_t *fb = (uint16_t*)calloc(sizeof(uint8_t), PPU::OutputBufferSize);
      ippu->CopyFrame((uint8_t*)fb);
      // Run it through a DefaultVideoFilter or NtscFilter
      filter.SendFrame(fb);
      // Dump it to file
      filter.TakeScreenshot(VideoFilterType::None, "out.png", NULL);
  }
  Console::Halt();
  return 0;
}
