//
// atari800_config.h
//
// Atari800 core configuration for BMX bare-metal
//

#ifndef atari800_config_h
#define atari800_config_h

namespace bmx {
namespace atari800 {

// True once emux_handle_loaded_setting has staged an explicit BASIC
// preference (defined in atari800_bridge.cpp).
bool basic_preference_loaded(void);

// Pushes settings-staged color values into Colours_setup (same source).
void apply_staged_colors(void);

// Rescans the staged /roms pool (see scan_rom_pool); callable at runtime
// from the System ROMs menu. Returns true when it rebooted the machine.
bool rescan_rom_pool(void);

// Filled SYSROM slots after the last scan (for "found N ROMs" feedback).
int last_rom_pool_matches(void);

// Clamps persisted ROM version preferences to available images.
void clamp_all_rom_prefs(void);

// Warp mode requested via the menu (same source).
int warp_enabled(void);

struct Config {
  // "-ntsc" or "-pal" (matches the machine timing option used by BMX).
  const char *tv_mode;
};

// Initializes the atari800 core via libatari800_init. Returns true on success.
bool config_init(const Config &config);

// True if the emulator is configured for NTSC.
bool config_is_ntsc(void);

// True once libatari800_init() has completed (machine may be reinitialised).
bool config_is_ready(void);

}  // namespace atari800
}  // namespace bmx

#endif
