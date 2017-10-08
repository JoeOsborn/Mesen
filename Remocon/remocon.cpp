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
      for(int i = 0; i < 600000; i++) {
        Console::RunOneStep();
      }
      uint16_t *fb = (uint16_t*)calloc(sizeof(uint8_t), PPU::OutputBufferSize);
      ippu->CopyFrame((uint8_t*)fb);
      printf("\n");
      for(int i = 0; i < PPU::ScreenHeight; i+=4) {
        for(int j = 0; j < PPU::ScreenWidth; j+=4) {
          int idx = i*PPU::ScreenWidth+j;
          printf("%d",fb[idx]);
        }
        printf("\n");
      }
      //diagnose fb, make sure the pattern is right
      // Run it through a DefaultVideoFilter or NtscFilter
      filter.SendFrame(fb);
      uint8_t *outputb = filter.GetOutputBuffer();
      //diagnose
      // Dump it to file
      filter.TakeScreenshot(VideoFilterType::None, "out.png", NULL);
  }
  Console::Halt();
  return 0;
}
