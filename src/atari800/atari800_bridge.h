//
// atari800_bridge.h
//
// Declares the emulator-specific API bridge between BMX's common code
// (menu/UI/kernel) and the atari800 emulator core, mirroring the role that
// vice_api.c plays for the VICE machines.
//

#ifndef atari800_bridge_h
#define atari800_bridge_h

namespace bmx {
namespace atari800 {

// The EMUX bridge needs no explicit install: emu_machine_init() (called by
// the common menu/kernel startup) publishes BMC64_MACHINE_CLASS_ATARI800.
// Kept as documentation anchor for the bridge module.

// Re-applies staged sound choices (channels/latency) via Sound_Setup.
// Called from sound_init at boot and live from the sound menu.
// Returns the Sound_Setup result.
bool apply_sound_setup(void);

// User-mapped function keys F6..F11 (Keyboard menu). Slot 0..5, action
// 0=None, 1=Help, 2=Start, 3=Select, 4=Option, 5=Reset, 6=Menu.
extern int g_fkey_action[6];

}  // namespace atari800
}  // namespace bmx

#endif
