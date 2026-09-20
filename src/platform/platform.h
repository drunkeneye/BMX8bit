#ifndef BMC64_PLATFORM_H
#define BMC64_PLATFORM_H

namespace bmc64 {

struct PlatformDescriptor {
  const char *name;
  int raspberry_pi_model;
  bool has_vchiq_audio;
  bool has_pi5kms;
  const char *default_kernel;
};

const PlatformDescriptor &CurrentPlatform();
int VolumePercentToDeviceControl(int percent);

}  // namespace bmc64

#endif
