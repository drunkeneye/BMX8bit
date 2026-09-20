#ifndef BMC64_MACHINE_DESCRIPTOR_H
#define BMC64_MACHINE_DESCRIPTOR_H

namespace bmc64 {

enum class MachineId {
  C64,
  SCPU64,
  C128,
  VIC20,
  PLUS4,
  PET,
  ATARI800,
};

struct MachineCycleProfile {
  int ntsc_hdmi;
  int ntsc_composite;
  int pal_hdmi;
  int pal_composite;
  int fallback;
};

struct MachineDescriptor {
  MachineId id;
  const char *display_name;
  const char *system_dir;
  const char *disk_dir;
  const char *bootstat_path;
  const char *kernel_suffix;
  const char *default_keymap;
  MachineCycleProfile cycles;
  bool has_vdc;
  bool legacy_sound_output_arg;
  const char *legacy_video_cache_arg_0;
  const char *legacy_video_cache_arg_1;
};

const MachineDescriptor &CurrentMachine();
int CurrentMachineCyclesPerSecond(int timing, unsigned long custom_cycles);

}  // namespace bmc64

#endif
