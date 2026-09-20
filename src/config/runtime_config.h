#ifndef BMC64_RUNTIME_CONFIG_H
#define BMC64_RUNTIME_CONFIG_H

namespace bmc64 {

bool ParseMachineTiming(const char *value, unsigned *timing);
bool TimingIsNtsc(unsigned timing);
bool TimingIsCustom(unsigned timing);
unsigned ApplyDpiToTiming(unsigned timing, bool dpi_enabled);
unsigned NormalizeCustomTiming(unsigned timing, unsigned long custom_cycles);
const char *ViceTimingOption(unsigned timing);

}  // namespace bmc64

#endif
