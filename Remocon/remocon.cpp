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
  std::fwrite("<\0",sizeof(uint8_t),2,stdout);
  for(int i = 0; i < 60; i++) {
    RunOneFrame(i % 2 == 0 ? (1 << 3) : (1 << 0), 0);
  }
  uint16_t *fb = (uint16_t*)calloc(sizeof(uint8_t), PPU::OutputBufferSize);
  ippu->CopyFrame((uint8_t*)fb);
  // Run it through a DefaultVideoFilter or NtscFilter
  filter.SendFrame(fb);
  // Dump it to file
  filter.TakeScreenshot(VideoFilterType::None, "out1.png", NULL);

  stringstream state;
  Console::SaveState(state);
  state.seekg(0, ios::beg);
  
  for(int i = 0; i < 240; i++) {
    RunOneFrame(1 << 7 | 1 << 1, 0);
  }
  ippu->CopyFrame((uint8_t*)fb);
  // Run it through a DefaultVideoFilter or NtscFilter
  filter.SendFrame(fb);
  // Dump it to file
  filter.TakeScreenshot(VideoFilterType::None, "out2.png", NULL);

  Console::LoadState(state);
  ippu->CopyFrame((uint8_t*)fb);
  // Run it through a DefaultVideoFilter or NtscFilter
  filter.SendFrame(fb);
  // Dump it to file
  filter.TakeScreenshot(VideoFilterType::None, "out3.png", NULL);

  RunOneFrame(0, 0);
  ippu->SendFrame();
  ippu->CopyFrame((uint8_t*)fb);
  // Run it through a DefaultVideoFilter or NtscFilter
  filter.SendFrame(fb);
  // Dump it to file
  filter.TakeScreenshot(VideoFilterType::None, "out4.png", NULL);
    
  Console::Halt();
  return 0;
}
