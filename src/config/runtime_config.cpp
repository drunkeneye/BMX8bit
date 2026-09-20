#include "config/runtime_config.h"

#include <string.h>

extern "C" {
#include "third_party/common/circle.h"
}

namespace bmc64 {

bool ParseMachineTiming(const char *value, unsigned *timing) {
  if (value == nullptr || timing == nullptr) {
    return false;
  }

  if (strcmp(value, "ntsc") == 0 || strcmp(value, "ntsc-hdmi") == 0) {
    *timing = MACHINE_TIMING_NTSC_HDMI;
  } else if (strcmp(value, "ntsc-dpi") == 0) {
    *timing = MACHINE_TIMING_NTSC_DPI;
  } else if (strcmp(value, "ntsc-composite") == 0) {
    *timing = MACHINE_TIMING_NTSC_COMPOSITE;
  } else if (strcmp(value, "ntsc-custom") == 0) {
    *timing = MACHINE_TIMING_NTSC_CUSTOM_HDMI;
  } else if (strcmp(value, "pal") == 0 || strcmp(value, "pal-hdmi") == 0) {
    *timing = MACHINE_TIMING_PAL_HDMI;
  } else if (strcmp(value, "pal-dpi") == 0) {
    *timing = MACHINE_TIMING_PAL_DPI;
  } else if (strcmp(value, "pal-composite") == 0) {
    *timing = MACHINE_TIMING_PAL_COMPOSITE;
  } else if (strcmp(value, "pal-custom") == 0) {
    *timing = MACHINE_TIMING_PAL_CUSTOM_HDMI;
  } else {
    return false;
  }

  return true;
}

bool TimingIsNtsc(unsigned timing) {
  return timing == MACHINE_TIMING_NTSC_HDMI ||
         timing == MACHINE_TIMING_NTSC_CUSTOM_HDMI ||
         timing == MACHINE_TIMING_NTSC_COMPOSITE ||
         timing == MACHINE_TIMING_NTSC_DPI ||
         timing == MACHINE_TIMING_NTSC_CUSTOM_DPI;
}

bool TimingIsCustom(unsigned timing) {
  return timing == MACHINE_TIMING_NTSC_CUSTOM_HDMI ||
         timing == MACHINE_TIMING_NTSC_CUSTOM_DPI ||
         timing == MACHINE_TIMING_PAL_CUSTOM_HDMI ||
         timing == MACHINE_TIMING_PAL_CUSTOM_DPI;
}

unsigned ApplyDpiToTiming(unsigned timing, bool dpi_enabled) {
  if (!dpi_enabled) {
    return timing;
  }

  if (timing == MACHINE_TIMING_PAL_CUSTOM_HDMI) {
    return MACHINE_TIMING_PAL_CUSTOM_DPI;
  }
  if (timing == MACHINE_TIMING_NTSC_CUSTOM_HDMI) {
    return MACHINE_TIMING_NTSC_CUSTOM_DPI;
  }

  return timing;
}

unsigned NormalizeCustomTiming(unsigned timing, unsigned long custom_cycles) {
  if (custom_cycles != 0) {
    return timing;
  }

  if (timing == MACHINE_TIMING_PAL_CUSTOM_HDMI) {
    return MACHINE_TIMING_PAL_HDMI;
  }
  if (timing == MACHINE_TIMING_NTSC_CUSTOM_HDMI) {
    return MACHINE_TIMING_NTSC_HDMI;
  }
  if (timing == MACHINE_TIMING_NTSC_CUSTOM_DPI) {
    return MACHINE_TIMING_NTSC_DPI;
  }
  if (timing == MACHINE_TIMING_PAL_CUSTOM_DPI) {
    return MACHINE_TIMING_PAL_DPI;
  }

  return timing;
}

const char *ViceTimingOption(unsigned timing) {
  return TimingIsNtsc(timing) ? "-ntsc" : "-pal";
}

}  // namespace bmc64
