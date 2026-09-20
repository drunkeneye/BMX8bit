//
// atari800_emulator_core.h
//
// Atari 800 emulator core integration for BMX bare-metal
//

#ifndef atari800_emulator_core_h
#define atari800_emulator_core_h

#include <circle/memory.h>
#include <circle/multicore.h>
#include <circle/spinlock.h>
#include "emulatorcore.h"

extern "C" {
#include "third_party/common/circle.h"
}

#ifndef BMX_EMU_MULTICORE
#define BMX_EMU_MULTICORE 0
#endif

#if defined(ARM_ALLOW_MULTI_CORE) && BMX_EMU_MULTICORE
#define BMC64_USE_EMU_MULTICORE 1
#endif

class Atari800EmulatorCore
 : public EmulatorCore
#ifdef BMC64_USE_EMU_MULTICORE
   ,public CMultiCoreSupport
#endif
{
public:
  Atari800EmulatorCore(CMemorySystem *pMemorySystem, int cyclesPerSecond);
  ~Atari800EmulatorCore(void);

  void Run(unsigned nCore)
#ifdef BMC64_USE_EMU_MULTICORE
      override
#endif
  ;

#ifndef BMC64_USE_EMU_MULTICORE
  bool Initialize() { return true; }
#endif

  bool Init(ViceOptions* options) override;
  void LaunchEmulator(char *timing_option) override;

private:
  bool launch_;
  int cyclesPerSecond_;
  char timing_option_[8];
  CSpinLock m_Lock;
  ViceOptions *m_options;

  void WaitForLaunch();
  void RunMainAtari800(bool wait);
  void RunFrameLoop();
};

#endif
