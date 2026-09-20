//
// atari800_bridge.cpp
//
// Emulator-specific API bridge between BMX common code (menu/UI/kernel) and
// the atari800 emulator core.  This fulfils the same role that
// third_party/vice-3.10/src/arch/raspi/vice_api.c plays for the VICE
// machines, but mapped onto libatari800.
//
// The contract is the shared emux_api.h.  Functions that are meaningful for
// atari800 are implemented against libatari800; the remaining VICE-specific
// facilities (REU, userport RS232, networking, wifi, updates, easyflood etc.)
// are given safe defaults so the kernel links and the common menu does not
// misbehave.
//

#include "atari800_bridge.h"
#include "atari800_config.h"
#include "atari800_debug.h"
#include "atari800_video.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

// The atari800 core headers are C headers; pull them in with C linkage.
// NOTE: atari.h first: it defines ULONG unconditionally, while
// libatari800.h guards on #ifndef ULONG. This order avoids a
// -Wredefined-macro warning with an identical definition either way.
extern "C" {
#include "third_party/atari800/src/atari.h"
#include "third_party/atari800/src/libatari800/libatari800.h"
#include "third_party/atari800/src/afile.h"
#include "third_party/atari800/src/antic.h"
#include "third_party/atari800/src/cartridge.h"
#include "third_party/atari800/src/cassette.h"
#include "third_party/atari800/src/colours.h"
#include "third_party/atari800/src/memory.h"
#include "third_party/atari800/src/screen.h"
#include "third_party/atari800/src/sound.h"
#include "third_party/atari800/src/sysrom.h"
#include "third_party/atari800/src/pokeysnd.h"
#include "third_party/atari800/src/rtime.h"
#include "third_party/atari800/src/binload.h"
#include "third_party/atari800/src/sio.h"
#include "third_party/atari800/src/compfile.h"
#include "third_party/atari800/src/cartridge_info.h"
#include "third_party/atari800/src/pia.h"
#include "third_party/atari800/src/gtia.h"
#include "third_party/atari800/src/input.h"
#include "third_party/atari800/src/pokey.h"
}

namespace bmx {
namespace atari800 {

// Defined in atari800_config.cpp.
bool config_is_ready(void);

// Defined below (needs the FBL/palette declarations from the C headers).
void apply_colors_to_core(void);

// Re-applies staged sound choices (channels/latency) via Sound_Setup.
// Returns the Sound_Setup result.
bool apply_sound_setup(void);

// Re-syncs all menu rows to live core state (build_menu runs before
// load_settings, so rows would otherwise show stale defaults).
void sync_all_menu_items(void);

// F6..F11 user actions (Keyboard menu): 0=None, 1=Help, 2=Start, 3=Select,
// 4=Option, 5=Reset, 6=Menu. Defined at bmx::atari800 scope so the input
// module reads it; menu rows mirror it.
int g_fkey_action[6] = {0, 0, 0, 0, 0, 0};

namespace {

// ---------------------------------------------------------------------------
// Timing / fps
// ---------------------------------------------------------------------------

double current_fps(void) {
  // libatari800 reports the configured frame rate directly.
  return (double)libatari800_get_fps();
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------

int request_sound_sample_rate(int sample_rate) {
  // The atari800 core uses its own fixed sample rate (Sound_desired is set up
  // in sound_init).  Ignore the requested rate and report the one in use.
  (void)sample_rate;
  return libatari800_get_sound_frequency();
}

// ---------------------------------------------------------------------------
// Machine settings (backing store for the BMX menu; mirrors the SDL TUI's
// Emulator Configuration). Applied live when the core is up, otherwise
// staged as globals for config_init.
// ---------------------------------------------------------------------------

// SIO drives D1:-D8: tracked for file_system_get_disk_name().
static char s_disk_names[8][256];

int sio_drive_from_unit(int unit) {
  // VICE drive units 8..15 map onto Atari SIO drives D1:..D8:.
  if (unit < 8 || unit > 15) {
    return -1;
  }
  return unit - 7;
}

void track_disk_name(int drive, const char *filename) {
  if (drive < 1 || drive > 8) {
    return;
  }
  if (filename == NULL) {
    s_disk_names[drive - 1][0] = '\0';
    return;
  }
  strncpy(s_disk_names[drive - 1], filename,
          sizeof(s_disk_names[drive - 1]) - 1);
  s_disk_names[drive - 1][sizeof(s_disk_names[drive - 1]) - 1] = '\0';
}

int default_ram_for_machine(int machine) {
  switch (machine) {
  case Atari800_MACHINE_800:
    return 48;
  case Atari800_MACHINE_5200:
    return 16;
  case Atari800_MACHINE_XLXE:
  default:
    return 64;
  }
}

// Machine submodels (SDL TUI parity): type + RAM + XEGS-game flag.
struct Submodel {
  const char *name;
  int type;
  int ram;
  int game;
};

static const Submodel kSubmodels[] = {
    {"Atari 400", Atari800_MACHINE_800, 16, 0},
    {"Atari 800", Atari800_MACHINE_800, 48, 0},
    {"Atari 800XL", Atari800_MACHINE_XLXE, 64, 0},
    {"Atari 1200XL", Atari800_MACHINE_XLXE, 64, 0},
    {"Atari 600XL", Atari800_MACHINE_XLXE, 16, 0},
    {"Atari 130XE", Atari800_MACHINE_XLXE, 128, 0},
    {"Atari 320XE", Atari800_MACHINE_XLXE, 192, 0},
    {"Atari XEGS", Atari800_MACHINE_XLXE, 64, 1},
    {"Atari 5200", Atari800_MACHINE_5200, 16, 0},
};
static const int kNumSubmodels =
    (int)(sizeof(kSubmodels) / sizeof(kSubmodels[0]));

// Forward: defined below (needs reinitialise_machine).
void apply_submodel(int sub);

// Staged color settings (VICE menu scale: 0..2000, neutral 1000;
// gamma 0..4000). Applied to Colours_setup once the core is up.
static int s_col_bri = 1000;
static int s_col_con = 1000;
static int s_col_sat = 1000;
static int s_col_tin = 1000;
static int s_col_gam = 1000;

// Staged sound settings (TUI Sound Settings parity). Channels/latency are
// applied to the core by apply_sound_setup (boot via sound_init, live from
// the menu). Buffer length is intentionally not exposed: the libatari800
// audio backend recomputes its frames and ignores buffer_ms. Frequency and
// bit depth stay fixed (44100 Hz / 16-bit) for the bare-metal sink.
static int s_snd_stereo = 0;
static int s_snd_latency = 20;

int ram_valid_for_machine(int machine, int ram) {
  switch (machine) {
  case Atari800_MACHINE_800:
    return ram == 8 || ram == 16 || ram == 48 || ram == 52;
  case Atari800_MACHINE_5200:
    return ram == 16;
  case Atari800_MACHINE_XLXE:
  default:
    return ram == 16 || ram == 64 || ram == 128 || ram == 192 ||
           ram == 320 || ram == 321 || ram == 576 || ram == 1088;
  }
}

void reinitialise_machine(void) {
  if (!config_is_ready()) {
    return;
  }
  Atari800_InitialiseMachine();
  Atari800_Coldstart();
}

void apply_submodel(int sub) {
  if (sub < 0 || sub >= kNumSubmodels) {
    return;
  }
  Atari800_machine_type = kSubmodels[sub].type;
  MEMORY_ram_size = kSubmodels[sub].ram;
  Atari800_builtin_game = kSubmodels[sub].game ? TRUE : FALSE;
  reinitialise_machine();
}

}  // namespace

}  // namespace atari800
}  // namespace bmx

// ===========================================================================
// C EMUX API (as declared in emux_api.h)
//
// emux_api.h is included inside the extern "C" block so its function and
// variable declarations share C linkage with the common library (emux_api.c)
// and the definitions below.
// ===========================================================================

extern "C" {

#include "emux_api.h"
#include "menu.h"
#include "menu_timing.h"

static void apply_loaded_machine(void);

// Strong override of zipbrowse.cpp's weak reboot-persistent trace hook:
// forwards to the synced Atari debug log (survives a hung extraction).
void zipbrowse_trace(const char *line) {
  if (line != NULL) {
    bmx::atari800::debug_log("zip: %s", line);
  }
}

}  // extern "C"

// Menu item handles (owned by the menu tree; valid while it is open).
// Defined here, after emux_api.h -> ui.h declares struct menu_item.
namespace bmx {
namespace atari800 {
namespace {

struct menu_item *s_machine_item = NULL;
struct menu_item *s_ram_item = NULL;

// Toggles/choices below are built from live core state, but build_menu
// runs before load_settings(): without a post-load sync the rows show
// compile-time defaults while the core runs the loaded values.
struct menu_item *s_basicen_item = NULL;
struct menu_item *s_sio_item = NULL;
struct menu_item *s_artifact_item = NULL;
struct menu_item *s_monitor_item = NULL;
struct menu_item *s_leddisk_item = NULL;
struct menu_item *s_ledsector_item = NULL;
struct menu_item *s_led1200_item = NULL;
struct menu_item *s_speed_item = NULL;
struct menu_item *s_rtime_item = NULL;
struct menu_item *s_slowboot_item = NULL;
struct menu_item *s_sound_item = NULL;
struct menu_item *s_stereo_item = NULL;
struct menu_item *s_hifi_item = NULL;
struct menu_item *s_click_item = NULL;
struct menu_item *s_bienias_item = NULL;
struct menu_item *s_latency_item = NULL;
struct menu_item *s_cartreboot_item = NULL;
struct menu_item *s_fkey_items[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
struct menu_item *s_cartblank_item = NULL;

void sync_ram_item(void) {
  if (s_ram_item == NULL) {
    return;
  }
  // Choice index of the current RAM size, or 0 if somehow unset.
  for (int i = 0; i < s_ram_item->num_choices; i++) {
    if (s_ram_item->choice_ints[i] == MEMORY_ram_size) {
      s_ram_item->value = i;
      return;
    }
  }
  s_ram_item->value = 0;
}

void sync_machine_item(void) {
  if (s_machine_item == NULL) {
    return;
  }
  for (int i = 0; i < s_machine_item->num_choices; i++) {
    int sub = s_machine_item->choice_ints[i];
    if (sub >= 0 && sub < kNumSubmodels &&
        kSubmodels[sub].type == Atari800_machine_type &&
        kSubmodels[sub].ram == MEMORY_ram_size &&
        kSubmodels[sub].game == (Atari800_builtin_game ? 1 : 0)) {
      s_machine_item->value = i;
      return;
    }
  }
  // Extended RAM sizes (320/576/1088) have no dedicated submodel: fall
  // back to the first entry with the same machine type + game flag so
  // the Machine row still reflects the running model.
  for (int i = 0; i < s_machine_item->num_choices; i++) {
    int sub = s_machine_item->choice_ints[i];
    if (sub >= 0 && sub < kNumSubmodels &&
        kSubmodels[sub].type == Atari800_machine_type &&
        kSubmodels[sub].game == (Atari800_builtin_game ? 1 : 0)) {
      s_machine_item->value = i;
      return;
    }
  }
  s_machine_item->value = 0;
}

// -- SIO drive menu state (SDL TUI Disk Management parity) -----------------

static const int kDriveInsIds[8] = {
    MENU_ATARI_DRV_INS_1, MENU_ATARI_DRV_INS_2, MENU_ATARI_DRV_INS_3,
    MENU_ATARI_DRV_INS_4, MENU_ATARI_DRV_INS_5, MENU_ATARI_DRV_INS_6,
    MENU_ATARI_DRV_INS_7, MENU_ATARI_DRV_INS_8,
};

static const int kDriveEjectIds[8] = {
    MENU_ATARI_EJECT_1, MENU_ATARI_EJECT_2, MENU_ATARI_EJECT_3,
    MENU_ATARI_EJECT_4, MENU_ATARI_EJECT_5, MENU_ATARI_EJECT_6,
    MENU_ATARI_EJECT_7, MENU_ATARI_EJECT_8,
};

static const int kDriveRoIds[8] = {
    MENU_ATARI_RO_1, MENU_ATARI_RO_2, MENU_ATARI_RO_3, MENU_ATARI_RO_4,
    MENU_ATARI_RO_5, MENU_ATARI_RO_6, MENU_ATARI_RO_7, MENU_ATARI_RO_8,
};

struct menu_item *s_drive_status[8] = {NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL};
struct menu_item *s_drive_ro[8] = {NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL};

static const char *drive_basename(const char *path) {
  const char *base = path;
  for (const char *p = path; *p != '\0'; p++) {
    if (*p == '/' || *p == '\\' || *p == ':') {
      base = p + 1;
    }
  }
  return base;
}

void sync_drive_items(void) {
  for (int d = 0; d < 8; d++) {
    int st = SIO_drive_status[d];
    if (s_drive_status[d] != NULL) {
      if (st == SIO_OFF) {
        snprintf(s_drive_status[d]->name, MAX_MENU_STR, "State: Off");
      } else if (st == SIO_NO_DISK) {
        snprintf(s_drive_status[d]->name, MAX_MENU_STR, "State: Empty");
      } else {
        snprintf(s_drive_status[d]->name, MAX_MENU_STR, "State: %.22s %s",
                 drive_basename(SIO_filename[d]),
                 st == SIO_READ_ONLY ? "(R/O)" : "(R/W)");
      }
    }
    if (s_drive_ro[d] != NULL) {
      s_drive_ro[d]->value = (st == SIO_READ_ONLY) ? 1 : 0;
      s_drive_ro[d]->disabled =
          (st == SIO_READ_ONLY || st == SIO_READ_WRITE) ? 0 : 1;
    }
  }
}

// -- System ROM selection (SDL TUI "System ROM Settings" parity) ----------

// Human labels for SYSROM slots shown in the version selectors.
struct RomChoice {
  int id;
  const char *name;
};

static const RomChoice kOs800[] = {
    {SYSROM_A_NTSC, "OS-A NTSC"},
    {SYSROM_A_PAL, "OS-A PAL"},
    {SYSROM_B_NTSC, "OS-B NTSC"},
    {SYSROM_800_CUSTOM, "Custom 800"},
    {SYSROM_ALTIRRA_800, "Altirra OS"},
};

static const RomChoice kOsXl[] = {
    {SYSROM_AA00R10, "1200XL AA00"},
    {SYSROM_AA01R11, "1200XL AA01"},
    {SYSROM_BB00R1, "600XL"},
    {SYSROM_BB01R2, "800XL rev2"},
    {SYSROM_BB02R3, "130XE rev3"},
    {SYSROM_BB02R3V4, "130XE rev3v4"},
    {SYSROM_CC01R4, "CC01 rev4"},
    {SYSROM_BB01R3, "XL rev3"},
    {SYSROM_BB01R4_OS, "XEGS rev4"},
    {SYSROM_BB01R59, "Arabic 59"},
    {SYSROM_BB01R59A, "Arabic 59a"},
    {SYSROM_XL_CUSTOM, "Custom XL"},
    {SYSROM_ALTIRRA_XL, "Altirra OS"},
};

static const RomChoice kOs5200[] = {
    {SYSROM_5200, "5200 BIOS"},
    {SYSROM_5200A, "5200 rev A"},
    {SYSROM_5200_CUSTOM, "Custom 5200"},
    {SYSROM_ALTIRRA_5200, "Altirra OS"},
};

static const RomChoice kBasic[] = {
    {SYSROM_BASIC_A, "BASIC rev A"},
    {SYSROM_BASIC_B, "BASIC rev B"},
    {SYSROM_BASIC_C, "BASIC rev C"},
    {SYSROM_BASIC_CUSTOM, "Custom BASIC"},
    {SYSROM_ALTIRRA_BASIC, "Altirra BASIC"},
};

static bool rom_available(int id) {
  if (id < 0 || id >= SYSROM_SIZE) {
    return false;
  }
  return SYSROM_roms[id].data != NULL || SYSROM_roms[id].filename[0] != '\0';
}

static const RomChoice *os_choices_for_machine_type(int machine, int *count) {
  switch (machine) {
  case Atari800_MACHINE_800:
    *count = (int)(sizeof(kOs800) / sizeof(kOs800[0]));
    return kOs800;
  case Atari800_MACHINE_5200:
    *count = (int)(sizeof(kOs5200) / sizeof(kOs5200[0]));
    return kOs5200;
  case Atari800_MACHINE_XLXE:
  default:
    *count = (int)(sizeof(kOsXl) / sizeof(kOsXl[0]));
    return kOsXl;
  }
}

static const RomChoice *os_choices_for_machine(int *count) {
  return os_choices_for_machine_type(Atari800_machine_type, count);
}

static int os_pref_for_machine(void) {
  if (Atari800_machine_type < 0 ||
      Atari800_machine_type >= Atari800_MACHINE_SIZE) {
    return SYSROM_AUTO;
  }
  return SYSROM_os_versions[Atari800_machine_type];
}

static struct menu_item *s_os_item = NULL;
static struct menu_item *s_basic_item = NULL;

// Falls back to AUTO when the persisted/chosen revision has no data or
// file behind it (prevents booting into a missing OS = black screen).
// Altirra entries always qualify (compiled in).
static int clamp_os_pref_for(int pref, int machine) {
  if (pref == SYSROM_AUTO) {
    return pref;
  }
  int count = 0;
  const RomChoice *choices = os_choices_for_machine_type(machine, &count);
  for (int i = 0; i < count; i++) {
    if (choices[i].id == pref && rom_available(pref)) {
      return pref;
    }
  }
  return SYSROM_AUTO;
}

static int clamp_os_pref(int pref) {
  return clamp_os_pref_for(pref, Atari800_machine_type);
}

static int clamp_basic_pref(int pref) {
  if (pref == SYSROM_AUTO) {
    return pref;
  }
  for (size_t i = 0; i < sizeof(kBasic) / sizeof(kBasic[0]); i++) {
    if (kBasic[i].id == pref && rom_available(pref)) {
      return pref;
    }
  }
  if (pref == SYSROM_ALTIRRA_BASIC) {
    return pref;
  }
  return SYSROM_AUTO;
}

void sync_rom_items(void) {
  if (s_os_item != NULL) {
    int pref = os_pref_for_machine();
    for (int i = 0; i < s_os_item->num_choices; i++) {
      if (s_os_item->choice_ints[i] == pref) {
        s_os_item->value = i;
        break;
      }
    }
  }
  if (s_basic_item != NULL) {
    for (int i = 0; i < s_basic_item->num_choices; i++) {
      if (s_basic_item->choice_ints[i] == SYSROM_basic_version) {
        s_basic_item->value = i;
        break;
      }
    }
  }
}

}  // namespace
}  // namespace atari800
}  // namespace bmx

// Set when the settings file stages an explicit BASIC preference; the
// emulator default-enables BASIC unless this is present. Lives outside
// the anonymous namespace: config_init links against it.
namespace bmx {
namespace atari800 {
bool g_basic_pref_loaded = false;

bool basic_preference_loaded(void) {
  return g_basic_pref_loaded;
}

// Clamps all persisted ROM version preferences against actually available
// images. Called from config_init after the /roms scan so a deleted file
// can never black-screen the boot.
void clamp_all_rom_prefs(void) {
  for (int m = 0; m < Atari800_MACHINE_SIZE; m++) {
    SYSROM_os_versions[m] = clamp_os_pref_for(SYSROM_os_versions[m], m);
  }
  SYSROM_basic_version = clamp_basic_pref(SYSROM_basic_version);
}

// Warp mode (menu toggle): run frames unpaced. Sound keeps playing;
// VICE additionally offers warp-muted audio, which we skip for now.
int g_warp = 0;

int warp_enabled(void) {
  return g_warp;
}

// Applies settings-staged color values once Colours_setup exists
// (called from config_init after libatari800_init).
void apply_staged_colors(void) {
  apply_colors_to_core();
}

namespace {

void sync_choice_to_int(struct menu_item *item, int id) {
  if (item == NULL) {
    return;
  }
  for (int i = 0; i < item->num_choices; i++) {
    if (item->choice_ints[i] == id) {
      item->value = i;
      return;
    }
  }
}

}  // namespace

void sync_all_menu_items(void) {
  if (s_basicen_item != NULL) {
    s_basicen_item->value = (Atari800_disable_basic == 0) ? 1 : 0;
  }
  if (s_sio_item != NULL) {
    s_sio_item->value = libatari800_get_sio_patch_enabled() ? 1 : 0;
  }
  sync_choice_to_int(s_artifact_item, ANTIC_artif_mode);
  sync_choice_to_int(s_monitor_item, video_get_monitor());
  if (s_leddisk_item != NULL) {
    s_leddisk_item->value = Screen_show_disk_led ? 1 : 0;
  }
  if (s_ledsector_item != NULL) {
    s_ledsector_item->value = Screen_show_sector_counter ? 1 : 0;
  }
  if (s_led1200_item != NULL) {
    s_led1200_item->value = Screen_show_1200_leds ? 1 : 0;
  }
  if (s_speed_item != NULL) {
    s_speed_item->value = Screen_show_atari_speed ? 1 : 0;
  }
  if (s_rtime_item != NULL) {
    s_rtime_item->value = RTIME_enabled ? 1 : 0;
  }
  if (s_slowboot_item != NULL) {
    s_slowboot_item->value = BINLOAD_slow_xex_loading ? 1 : 0;
  }
  if (s_sound_item != NULL) {
    s_sound_item->value = Sound_enabled ? 1 : 0;
  }
  if (s_stereo_item != NULL) {
    s_stereo_item->value = s_snd_stereo ? 1 : 0;
  }
  if (s_hifi_item != NULL) {
    s_hifi_item->value = POKEYSND_enable_new_pokey ? 1 : 0;
  }
  if (s_click_item != NULL) {
    s_click_item->value = POKEYSND_console_sound_enabled ? 1 : 0;
  }
  if (s_bienias_item != NULL) {
    s_bienias_item->value = POKEYSND_bienias_fix ? 1 : 0;
  }
  if (s_latency_item != NULL) {
    s_latency_item->value = s_snd_latency;
  }
  if (s_cartreboot_item != NULL) {
    s_cartreboot_item->value = CARTRIDGE_autoreboot ? 1 : 0;
  }
  for (int i = 0; i < 6; i++) {
    if (s_fkey_items[i] != NULL) {
      int action = g_fkey_action[i];
      if (action < 0 || action > 6) {
        action = 0;
      }
      s_fkey_items[i]->value = action;
    }
  }
  sync_machine_item();
  sync_ram_item();
  sync_rom_items();
  sync_drive_items();
}
}  // namespace atari800
}  // namespace bmx

extern "C" {

// -- machine ---------------------------------------------------------------

void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled) {
  (void)raster_skip_enabled;
  (void)raster_skip2_enabled;
  emux_machine_class = BMC64_MACHINE_CLASS_ATARI800;
  emux_c64_core = BMC64_C64_CORE_UNKNOWN;
}

int emu_ui_uses_german_keyboard_layout(void) {
  return 0;
}

unsigned long emux_calculate_timing(double fps) {
  // No vsync-based timing adjustment for atari800; the frame loop self-paces.
  return 0;
}

double emux_calculate_fps(void) {
  return bmx::atari800::current_fps();
}

// -- sound -----------------------------------------------------------------

int emu_set_sound_sample_rate(int sample_rate) {
  return bmx::atari800::request_sound_sample_rate(sample_rate);
}

// -- reset / disks / state -------------------------------------------------

void emux_reset(int isSoft) {
  if (isSoft) {
    Atari800_Warmstart();
  } else {
    Atari800_Coldstart();
  }
}

int emux_attach_disk_image(int unit, char *filename) {
  // VICE convention: 0 on success, -1 on failure.
  int drive = bmx::atari800::sio_drive_from_unit(unit);
  if (drive < 0 || filename == NULL) {
    return -1;
  }
  if (libatari800_mount_disk(drive, filename, 0) == 0) {
    return -1;
  }
  bmx::atari800::track_disk_name(drive, filename);
  return 0;
}

void emux_detach_disk(int unit) {
  int drive = bmx::atari800::sio_drive_from_unit(unit);
  if (drive < 0) {
    return;
  }
  libatari800_unmount_disk(drive);
  bmx::atari800::track_disk_name(drive, NULL);
}

// -- SIO disk-set / blank / uncompress (menu.c file flows) ----------------
// Mirrors the SDL TUI Disk Management actions (ui.c): 8-line text sets with
// Empty/Off markers, 720-sector blank ATRs, first-byte sniff conversion.

static void retrack_all_drives(void) {
  for (int d = 1; d <= 8; d++) {
    const char *fn = SIO_filename[d - 1];
    if (strcmp(fn, "Empty") == 0 || strcmp(fn, "Off") == 0) {
      bmx::atari800::track_disk_name(d, NULL);
    } else {
      bmx::atari800::track_disk_name(d, fn);
    }
  }
}

int emux_atari_mount_disk(int drive, char *path) {
  if (drive < 1 || drive > 8 || path == NULL) {
    return -1;
  }
  bmx::atari800::debug_log("zip: mount D%d %s", drive, path);
  if (!libatari800_mount_disk(drive, path, 0)) {
    bmx::atari800::debug_log("zip: mount failed");
    return -1;
  }
  bmx::atari800::track_disk_name(drive, path);
  bmx::atari800::sync_drive_items();
  bmx::atari800::debug_log("zip: mount ok");
  return 0;
}

void emux_atari_sync_drives(void) { bmx::atari800::sync_drive_items(); }

static void ensure_ext(char *dst, size_t size, const char *path,
                       const char *ext) {
  snprintf(dst, size, "%s", path);
  const char *slash = strrchr(dst, '/');
  const char *base = slash != NULL ? slash + 1 : dst;
  if (strchr(base, '.') == NULL) {
    strncat(dst, ext, size - strlen(dst) - 1);
  }
}

int emux_atari_save_set(char *path) {
  if (path == NULL) {
    return -1;
  }
  char fixed[300];
  ensure_ext(fixed, sizeof(fixed), path, ".ats");
  FILE *fp = fopen(fixed, "w");
  if (fp == NULL) {
    return -1;
  }
  for (int i = 0; i < 8; i++) {
    fprintf(fp, "%s\n", SIO_filename[i]);
  }
  fclose(fp);
  return 0;
}

int emux_atari_load_set(char *path) {
  if (path == NULL) {
    return -1;
  }
  FILE *fp = fopen(path, "r");
  if (fp == NULL) {
    return -1;
  }
  char line[300];
  for (int i = 0; i < 8; i++) {
    if (fgets(line, sizeof(line), fp) == NULL) {
      break;
    }
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
      line[--n] = '\0';
    }
    if (line[0] == '\0' || strcmp(line, "Empty") == 0 ||
        strcmp(line, "Off") == 0) {
      continue;
    }
    if (libatari800_mount_disk(i + 1, line, 0)) {
      bmx::atari800::track_disk_name(i + 1, line);
    }
  }
  fclose(fp);
  retrack_all_drives();
  bmx::atari800::sync_drive_items();
  return 0;
}

int emux_atari_blank_atr(char *path) {
  // TUI MakeBlankDisk: standard 720-sector single-density ATR (DOS formats
  // it, growing the image); 16-byte header + zero sectors.
  if (path == NULL) {
    return -1;
  }
  char fixed[300];
  ensure_ext(fixed, sizeof(fixed), path, ".atr");
  FILE *fp = fopen(fixed, "wb");
  if (fp == NULL) {
    return -1;
  }
  static const unsigned char hdr[16] = {0x96, 0x02, 0x80, 0x16, 0x80, 0x00,
                                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00};
  bool ok = fwrite(hdr, 1, sizeof(hdr), fp) == sizeof(hdr);
  unsigned char zero[128];
  memset(zero, 0, sizeof(zero));
  for (int i = 0; i < 720 && ok; i++) {
    ok = fwrite(zero, 1, sizeof(zero), fp) == sizeof(zero);
  }
  fclose(fp);
  return ok ? 0 : -1;
}

static void swap_ext_case(char *base, const char *newext) {
  // Replace base's extension with newext, preserving each char's case.
  char *dot = strrchr(base, '.');
  if (dot == NULL) {
    strcat(base, ".");
    strcat(base, newext);
    return;
  }
  size_t i = 0;
  dot++;
  while (newext[i] != '\0' && i < 8) {
    dot[i] = (dot[i] >= 'A' && dot[i] <= 'Z' && newext[i] >= 'a')
                 ? (char)(newext[i] - ('a' - 'A'))
                 : newext[i];
    i++;
  }
  dot[i] = '\0';
}

int emux_atari_uncompress(char *path, char *out_path, size_t out_size) {
  if (path == NULL || out_path == NULL || out_size == 0) {
    return -1;
  }
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return -1;
  }
  int magic = fgetc(fp);
  const char *slash = strrchr(path, '/');
  size_t dirlen = slash != NULL ? (size_t)(slash - path + 1) : 0;
  const char *base = slash != NULL ? slash + 1 : path;
  char newbase[128];
  snprintf(newbase, sizeof(newbase), "%s", base);
  int is_gz = (magic == 0x1f);
  int is_dcm = (magic == 0xf9 || magic == 0xfa);
  if (is_gz) {
    // .gz strips, .atz -> .atr, .xfz -> .xfd (TUI propose rules).
    const char *dot = strrchr(newbase, '.');
    if (dot != NULL &&
        (strcmp(dot + 1, "gz") == 0 || strcmp(dot + 1, "GZ") == 0)) {
      *((char *)dot) = '\0';
    } else if (dot != NULL &&
               (strcmp(dot + 1, "atz") == 0 || strcmp(dot + 1, "ATZ") == 0)) {
      swap_ext_case(newbase, "atr");
    } else if (dot != NULL &&
               (strcmp(dot + 1, "xfz") == 0 || strcmp(dot + 1, "XFZ") == 0)) {
      swap_ext_case(newbase, "xfd");
    }
  } else if (is_dcm) {
    swap_ext_case(newbase, "atr");
  } else {
    fclose(fp);
    return -1;
  }
  snprintf(out_path, out_size, "%.*s%s", (int)dirlen, path, newbase);
  FILE *out = fopen(out_path, "wb");
  if (out == NULL) {
    fclose(fp);
    return -1;
  }
  int ok;
  if (is_gz) {
    ok = CompFile_ExtractGZ(path, out);
  } else {
    rewind(fp);
    ok = CompFile_DCMtoATR(fp, out);
  }
  fclose(out);
  fclose(fp);
  return ok ? 0 : -1;
}

int emux_attach_tape_image(char *filename) {
  if (filename == NULL || CASSETTE_Insert(filename) == 0) {
    return -1;
  }
  return 0;
}

void emux_detach_tape(void) {
  CASSETTE_Remove();
}

int emux_attach_cart(int bank, char *filename) {
  (void)bank;
  if (filename == NULL) {
    return -1;
  }
  // AutoReboot variant: like the TUI, the machine reboots so the new
  // cartridge takes effect immediately (gated by CARTRIDGE_autoreboot).
  // NOTE: Insert returns 0/BAD_CHECKSUM/size-KB when mounted, negative
  // CANT_OPEN/BAD_FORMAT/TOO_FEW_DATA on failure (the old code had this
  // inverted, masked because the caller ignores the result).
  bmx::atari800::debug_log("zip: cart attach %s", filename);
  int r = CARTRIDGE_InsertAutoReboot(filename);
  bmx::atari800::debug_log("zip: cart attach result=%d", r);
  if (r == CARTRIDGE_CANT_OPEN || r == CARTRIDGE_BAD_FORMAT ||
      r == CARTRIDGE_TOO_FEW_DATA) {
    return -1;
  }
  return 0;
}

void emux_detach_cart(int bank) {
  (void)bank;
  CARTRIDGE_RemoveAutoReboot();
}

void emux_set_cart_default(void) {
  CARTRIDGE_Remove();
}

// Snapshot format: 8-byte magic + raw emulator_state_t. The struct is a
// fixed-size blob (no pointers), so a plain file round-trip is safe.
static const char kStateMagic[8] = {'A', '8', '0', '0', 'B', 'M', 'X', '1'};

int emux_load_state(char *filename) {
  if (filename == NULL) {
    return -1;
  }
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    return -1;
  }
  char magic[sizeof(kStateMagic)];
  int ok = 0;
  if (fread(magic, 1, sizeof(magic), fp) == sizeof(magic) &&
      memcmp(magic, kStateMagic, sizeof(magic)) == 0) {
    emulator_state_t *state =
        (emulator_state_t *)malloc(sizeof(emulator_state_t));
    if (state != NULL) {
      if (fread(state, 1, sizeof(*state), fp) == sizeof(*state)) {
        libatari800_restore_state(state);
        ok = 1;
      }
      free(state);
    }
  }
  fclose(fp);
  return ok ? 0 : -1;
}

int emux_save_state(char *filename) {
  if (filename == NULL) {
    return -1;
  }
  emulator_state_t *state =
      (emulator_state_t *)malloc(sizeof(emulator_state_t));
  if (state == NULL) {
    return -1;
  }
  libatari800_get_current_state(state);
  FILE *fp = fopen(filename, "wb");
  if (fp == NULL) {
    free(state);
    return -1;
  }
  size_t wrote = fwrite(kStateMagic, 1, sizeof(kStateMagic), fp);
  wrote += fwrite(state, 1, sizeof(*state), fp);
  fclose(fp);
  free(state);
  return wrote == sizeof(kStateMagic) + sizeof(*state) ? 0 : -1;
}

int emux_load_reu_image(char *filename) {
  (void)filename;
  return 0;
}

int emux_save_reu_image(char *filename) {
  (void)filename;
  return 0;
}

int emux_tape_control(int cmd) {
  (void)cmd;
  return 0;
}

void emux_show_cart_osd_menu(void) {}

int emux_autostart_file(char *filename, unsigned int program_number) {
  (void)program_number;
  // Boot the machine from file (used for autostart / ROM change flows).
  // libatari800_reboot_with_file returns the detected AFILE_* type or
  // AFILE_ERROR on failure.
  if (filename == NULL ||
      libatari800_reboot_with_file(filename) == AFILE_ERROR) {
    return -1;
  }
  return 0;
}

void emux_drive_change_model(int unit) {
  (void)unit;
}

void emux_set_warp(int warp) {
  bmx::atari800::g_warp = warp ? 1 : 0;
}

int emux_prepare_shutdown(void) {
  // VICE convention: 0 on success (flushes REU there). Close our SD debug
  // log so CGlueStdioShutdown's fclose-all succeeds during reboot/poweroff.
  // (A nonzero return aborts the whole shutdown with "Problem closing
  // emulator files".)
  bmx::atari800::debug_close();
  return 0;
}

void emux_set_iec_dir(int unit, char *dir) {
  (void)unit;
  (void)dir;
}

// -- input -----------------------------------------------------------------

void emux_set_joy_port_device(int port_num, int dev_id) {
  (void)port_num;
  (void)dev_id;
}

void emux_set_joy_pot_x(int port, int value) {
  (void)port;
  (void)value;
}

void emux_set_joy_pot_y(int port, int value) {
  (void)port;
  (void)value;
}

void emux_mouse_input_clear(void) {}

int emux_mouse_preview_poll(float *delta_x, float *delta_y) {
  (void)delta_x;
  (void)delta_y;
  return 0;
}

void emux_kbd_set_latch_keyarr(int row, int col, int value) {
  (void)row;
  (void)col;
  (void)value;
}

void emux_keyboard_type_changed(void) {}

const char *emux_keyboard_mapping_file(void) {
  return NULL;
}

int emux_keyboard_mapping_lookup(long keycode, unsigned char usb_modifiers,
                                 int *row, int *column, int *flags) {
  (void)keycode;
  (void)usb_modifiers;
  (void)row;
  (void)column;
  (void)flags;
  return 0;
}

int emux_keyboard_mapping_target_name(int row, int column, int flags,
                                      char *buffer, size_t buffer_size) {
  (void)row;
  (void)column;
  (void)flags;
  (void)buffer;
  (void)buffer_size;
  return 0;
}

int emux_keymap_editor_begin(struct keymap_editor_model *model, int *editable,
                             char *error, size_t error_size) {
  (void)model;
  (void)editable;
  (void)error;
  (void)error_size;
  return 0;
}

int emux_keymap_editor_save(const struct keymap_editor_model *model,
                            char *error, size_t error_size) {
  (void)model;
  (void)error;
  (void)error_size;
  return 0;
}

int emux_keymap_editor_restore_defaults(char *error, size_t error_size) {
  (void)error;
  (void)error_size;
  return 0;
}

// -- virtual keyboard ------------------------------------------------------

vkbd_key_array emux_get_vkbd(void) {
  return NULL;
}

int emux_get_vkbd_width(void) {
  return 0;
}

int emux_get_vkbd_height(void) {
  return 0;
}

int emux_get_vkbd_size(void) {
  return 0;
}

// -- traps / main loop -----------------------------------------------------

// Runs the BMX menu on the emulator thread while it is open, mirroring
// emu_pause_trap() (the VICE path): emulation pauses, the UI layer shows,
// menu keys render. Returns when the menu closes (F12/ESC) and emulation
// resumes. NOTE: no emux_ensure_video() here -- it programs VICE canvas
// state (vic_enabled/vic_showing in videoarch, unlinked in ATARI800
// builds). The VIC layer stays as the frame loop left it.
void emux_trap_main_loop_ui(void) {
  bmx::atari800::debug_log("Atari800: menu opened, entering pump");
  // Refresh VIC/UI geometry on every open: video_init's one-shot call can
  // race display bring-up (zeros are rejected and never retried). By open
  // time the output is up, so the UI layer sizes itself correctly.
  {
    int dpx = 0, dpy = 0, fbw = 0, fbh = 0, sw = 0, sh = 0, dw = 0, dh = 0;
    circle_get_fbl_dimensions(FB_LAYER_VIC, &dpx, &dpy, &fbw, &fbh, &sw, &sh,
                              &dw, &dh);
    bmx::atari800::debug_log("Atari800: menu geometry dpx=%d dpy=%d", dpx,
                             dpy);
    emux_geometry_changed(FB_LAYER_VIC);
  }
  menu_about_to_activate();
  circle_show_fbl(FB_LAYER_UI);
  {
    int dpx = 0, dpy = 0, fbw = 0, fbh = 0, sw = 0, sh = 0, dw = 0, dh = 0;
    circle_get_fbl_dimensions(FB_LAYER_UI, &dpx, &dpy, &fbw, &fbh, &sw, &sh,
                              &dw, &dh);
    bmx::atari800::debug_log(
        "Atari800: UI layer dpx=%d dpy=%d fb=%dx%d src=%dx%d dst=%dx%d "
        "ui_showing=%d",
        dpx, dpy, fbw, fbh, sw, sh, dw, dh, ui_showing);
  }
  while (ui_enabled) {
    circle_check_gpio();
    ui_check_key();
    ui_handle_toggle_or_quick_func();
    ui_render_single_frame();
    hdmi_timing_hook();
    // Same slot as VICE's emu_pause_trap: re-shows layers hidden by video
    // adjustments so stretch/border changes preview live instead of only
    // after the menu closes.
    emux_ensure_video();
  }
  menu_about_to_deactivate();
  circle_hide_fbl(FB_LAYER_UI);
  bmx::atari800::debug_log("Atari800: menu pump exit, resuming emulation");
}

void emux_trap_main_loop(void (*trap_func)(uint16_t, void *data), void *data) {
  (void)trap_func;
  (void)data;
}

// -- VICE glue helpers referenced by kernel.cpp ----------------------------

// Tracked SIO drive image names (see track_disk_name above).

const char *file_system_get_disk_name(unsigned int unit, unsigned int drive) {
  (void)drive;
  int sio = bmx::atari800::sio_drive_from_unit((int)unit);
  if (sio < 1 || sio > 8 ||
      bmx::atari800::s_disk_names[sio - 1][0] == '\0') {
    return NULL;
  }
  return bmx::atari800::s_disk_names[sio - 1];
}

const char *tape_get_file_name(int port) {
  (void)port;
  return NULL;
}

char *cartridge_get_filename_by_slot(int slot) {
  (void)slot;
  return NULL;
}

// No PETSCII character set on the Atari; return the buffer unchanged.
uint8_t *charset_petconvstring(uint8_t *text, int mode) {
  (void)mode;
  return text;
}

// No keyboard buffer feed for atari800; report no consumption.
int kbdbuf_feed(const char *text) {
  (void)text;
  return 0;
}

// VICE util used by the shared menu/UI code.
void lib_free(void *ptr) {
  free(ptr);
}

// -- settings / video / palette --------------------------------------------

void emux_add_drive_option(struct menu_item *parent, int drive) {
  // SIO drives D1:-D8: plus disk-set/rotate/blank/uncompress management
  // (SDL TUI Disk Management parity). Built here (not in the common menu)
  // so labels can sync against SIO_drive_status. Per-unit calls are unused.
  if (drive != -1) {
    return;
  }
  for (int d = 0; d < 8; d++) {
    bmx::atari800::s_drive_status[d] = NULL;
    bmx::atari800::s_drive_ro[d] = NULL;
    char folder_name[16];
    snprintf(folder_name, sizeof(folder_name), "Drive D%d", d + 1);
    struct menu_item *folder = ui_menu_add_folder(parent, folder_name);
    bmx::atari800::s_drive_status[d] =
        ui_menu_add_button(MENU_TEXT, folder, "State");
    ui_menu_add_button(bmx::atari800::kDriveInsIds[d], folder,
                       "Insert disk...");
    ui_menu_add_button(bmx::atari800::kDriveEjectIds[d], folder, "Eject");
    bmx::atari800::s_drive_ro[d] = ui_menu_add_toggle(
        bmx::atari800::kDriveRoIds[d], folder, "Read-only", 0);
  }
  ui_menu_add_button(MENU_ATARI_ROTATE, parent, "Rotate disks");
  ui_menu_add_button(MENU_ATARI_SET_SAVE, parent, "Save disk set...");
  ui_menu_add_button(MENU_ATARI_SET_LOAD, parent, "Load disk set...");
  ui_menu_add_button(MENU_ATARI_BLANK, parent, "Make blank ATR disk...");
  ui_menu_add_button(MENU_ATARI_UNCOMPRESS, parent,
                     "Uncompress disk image...");
  bmx::atari800::sync_drive_items();
}

void emux_add_keyboard_options(struct menu_item *parent) {
  // User-mapped function keys F6..F11 (F1..F5 are fixed Atari console keys,
  // F12 is the BMX menu). Actions: None/Help/Start/Select/Option/Reset/Menu.
  static const char *const names[6] = {"F6 Hotkey", "F7 Hotkey", "F8 Hotkey",
                                       "F9 Hotkey", "F10 Hotkey",
                                       "F11 Hotkey"};
  static const char *const actions[7] = {"None",  "Help",  "Start", "Select",
                                         "Option", "Reset", "Menu"};
  for (int i = 0; i < 6; i++) {
    char label[16];
    snprintf(label, sizeof(label), "%s", names[i]);
    struct menu_item *child = bmx::atari800::s_fkey_items[i] =
        ui_menu_add_multiple_choice(MENU_ATARI_F6 + i, parent, label);
    child->num_choices = 7;
    for (int a = 0; a < 7; a++) {
      strcpy(child->choices[a], actions[a]);
      child->choice_ints[a] = a;
    }
    child->value = bmx::atari800::g_fkey_action[i];
  }
}

void emux_add_tape_options(struct menu_item *parent) {
  (void)parent;
}

void emux_add_sound_options(struct menu_item *emulation_parent,
                            struct menu_item *sid_parent,
                            struct menu_item *sound_parent) {
  // No SID on the Atari; the TUI Sound Settings live here instead:
  // dual POKEY, fidelity/click/fix flags, output latency. Frequency,
  // bit depth and buffer length stay fixed for the bare-metal sink
  // (44100 Hz / 16-bit; the lib audio backend ignores buffer_ms).
  (void)sid_parent;
  (void)sound_parent;
  bmx::atari800::s_sound_item = NULL;
  bmx::atari800::s_stereo_item = NULL;
  bmx::atari800::s_hifi_item = NULL;
  bmx::atari800::s_click_item = NULL;
  bmx::atari800::s_bienias_item = NULL;
  bmx::atari800::s_latency_item = NULL;
  bmx::atari800::s_sound_item =
      ui_menu_add_toggle(MENU_ATARI_SOUND, emulation_parent,
                         "Sound Emulation", Sound_enabled ? 1 : 0);
  bmx::atari800::s_stereo_item =
      ui_menu_add_toggle(MENU_ATARI_STEREO, emulation_parent,
                         "Dual POKEY (Stereo)",
                         bmx::atari800::s_snd_stereo ? 1 : 0);
  bmx::atari800::s_hifi_item =
      ui_menu_add_toggle(MENU_ATARI_HIFI, emulation_parent,
                         "High Fidelity POKEY",
                         POKEYSND_enable_new_pokey ? 1 : 0);
  bmx::atari800::s_click_item =
      ui_menu_add_toggle(MENU_ATARI_KEYCLICK, emulation_parent,
                         "Speaker (Key Click)",
                         POKEYSND_console_sound_enabled ? 1 : 0);
  bmx::atari800::s_bienias_item =
      ui_menu_add_toggle(MENU_ATARI_BIENIAS, emulation_parent,
                         "Higher frequencies fix",
                         POKEYSND_bienias_fix ? 1 : 0);
  bmx::atari800::s_latency_item =
      ui_menu_add_range(MENU_ATARI_LATENCY, emulation_parent, "Latency (ms)",
                        0, 100, 1, bmx::atari800::s_snd_latency);
}

void emux_add_reu_options(struct menu_item *parent) {
  (void)parent;
}

void emux_add_machine_options(struct menu_item *parent) {
  // Mirrors the SDL TUI's Emulator Configuration: machine model, memory,
  // BASIC, SIO acceleration, GTIA artifacting and on-screen indicators.
  // TV mode stays owned by BMX (machines.ini timing), like VICE machines.
  bmx::atari800::debug_log("Atari800: building machine menu options");
  bmx::atari800::s_machine_item = NULL;
  bmx::atari800::s_ram_item = NULL;
  bmx::atari800::s_basicen_item = NULL;
  bmx::atari800::s_sio_item = NULL;
  bmx::atari800::s_artifact_item = NULL;
  bmx::atari800::s_monitor_item = NULL;
  bmx::atari800::s_leddisk_item = NULL;
  bmx::atari800::s_ledsector_item = NULL;
  bmx::atari800::s_led1200_item = NULL;
  bmx::atari800::s_speed_item = NULL;
  bmx::atari800::s_rtime_item = NULL;
  bmx::atari800::s_slowboot_item = NULL;

  struct menu_item *child = bmx::atari800::s_machine_item =
      ui_menu_add_multiple_choice(MENU_ATARI_MACHINE, parent, "Machine");
  child->num_choices = bmx::atari800::kNumSubmodels;
  for (int i = 0; i < bmx::atari800::kNumSubmodels; i++) {
    strcpy(child->choices[i], bmx::atari800::kSubmodels[i].name);
    child->choice_ints[i] = i;
  }
  bmx::atari800::sync_machine_item();

  child = bmx::atari800::s_ram_item =
      ui_menu_add_multiple_choice(MENU_ATARI_RAM, parent, "RAM size");
  // Superset across models; invalid picks are clamped on apply.
  // 321 = 320KB Compy Shop (distinct MEMORY size id, needs own label).
  static const int ram_options[] = {8,   16,  48,  52,   64,   128,
                                    192, 320, 321, 576, 1088};
  child->num_choices = 11;
  for (int i = 0; i < 11; i++) {
    if (ram_options[i] == 321) {
      snprintf(child->choices[i], MAX_MENU_STR, "320K CS");
    } else {
      snprintf(child->choices[i], MAX_MENU_STR, "%dK", ram_options[i]);
    }
    child->choice_ints[i] = ram_options[i];
  }
  bmx::atari800::sync_ram_item();

  bmx::atari800::s_basicen_item = ui_menu_add_toggle(
      MENU_ATARI_BASIC, parent, "BASIC enabled",
      Atari800_disable_basic == 0 ? 1 : 0);
  bmx::atari800::s_sio_item = ui_menu_add_toggle(
      MENU_ATARI_SIO_PATCH, parent, "SIO patch (fast disk)",
      libatari800_get_sio_patch_enabled() ? 1 : 0);
  bmx::atari800::s_rtime_item = ui_menu_add_toggle(
      MENU_ATARI_RTIME, parent, "R-Time 8 (RTC)", RTIME_enabled ? 1 : 0);
  bmx::atari800::s_slowboot_item =
      ui_menu_add_toggle(MENU_ATARI_SLOWBOOT, parent, "Slow XEX boot",
                         BINLOAD_slow_xex_loading ? 1 : 0);

  child = bmx::atari800::s_artifact_item =
      ui_menu_add_multiple_choice(MENU_ATARI_ARTIFACT, parent, "Artifacting");
  child->num_choices = 5;
  strcpy(child->choices[0], "Off");
  child->choice_ints[0] = 0;
  strcpy(child->choices[1], "Mode 1");
  child->choice_ints[1] = 1;
  strcpy(child->choices[2], "Mode 2");
  child->choice_ints[2] = 2;
  strcpy(child->choices[3], "Mode 3");
  child->choice_ints[3] = 3;
  strcpy(child->choices[4], "Mode 4");
  child->choice_ints[4] = 4;
  for (int i = 0; i < child->num_choices; i++) {
    if (child->choice_ints[i] == ANTIC_artif_mode) {
      child->value = i;
      break;
    }
  }

  child = bmx::atari800::s_monitor_item =
      ui_menu_add_multiple_choice(MENU_ATARI_MONITOR, parent, "Monitor");
  child->num_choices = 4;
  strcpy(child->choices[0], "Color");
  child->choice_ints[0] = bmx::atari800::ATARI_MONITOR_COLOR;
  strcpy(child->choices[1], "B&W");
  child->choice_ints[1] = bmx::atari800::ATARI_MONITOR_BW;
  strcpy(child->choices[2], "Green");
  child->choice_ints[2] = bmx::atari800::ATARI_MONITOR_GREEN;
  strcpy(child->choices[3], "Amber");
  child->choice_ints[3] = bmx::atari800::ATARI_MONITOR_AMBER;
  for (int i = 0; i < child->num_choices; i++) {
    if (child->choice_ints[i] == bmx::atari800::video_get_monitor()) {
      child->value = i;
      break;
    }
  }

  bmx::atari800::s_leddisk_item = ui_menu_add_toggle(
      MENU_ATARI_LED_DISK, parent, "Show disk LED",
      Screen_show_disk_led ? 1 : 0);
  bmx::atari800::s_ledsector_item = ui_menu_add_toggle(
      MENU_ATARI_LED_SECTOR, parent, "Show sector counter",
      Screen_show_sector_counter ? 1 : 0);
  bmx::atari800::s_led1200_item = ui_menu_add_toggle(
      MENU_ATARI_LED_1200, parent, "Show 1200XL LEDs",
      Screen_show_1200_leds ? 1 : 0);
  bmx::atari800::s_speed_item = ui_menu_add_toggle(
      MENU_ATARI_SPEED, parent, "Show speed",
      Screen_show_atari_speed ? 1 : 0);
  ui_menu_add_button(MENU_ATARI_CPUSTATE, parent, "Log CPU state");

  struct menu_item *roms = ui_menu_add_folder(parent, "System ROMs");
  ui_menu_add_button(MENU_ATARI_ROM_RESCAN, roms, "Scan /roms for ROMs");
  bmx::atari800::s_os_item = NULL;
  bmx::atari800::s_basic_item = NULL;
  child = bmx::atari800::s_os_item =
      ui_menu_add_multiple_choice(MENU_ATARI_OS_VER, roms, "OS version");
  {
    int count = 0;
    const bmx::atari800::RomChoice *choices =
        bmx::atari800::os_choices_for_machine(&count);
    child->num_choices = 0;
    strcpy(child->choices[child->num_choices], "Auto");
    child->choice_ints[child->num_choices] = SYSROM_AUTO;
    child->num_choices++;
    for (int i = 0; i < count; i++) {
      strcpy(child->choices[child->num_choices], choices[i].name);
      child->choice_ints[child->num_choices] = choices[i].id;
      child->num_choices++;
    }
  }
  bmx::atari800::sync_rom_items();
  child = bmx::atari800::s_basic_item =
      ui_menu_add_multiple_choice(MENU_ATARI_BASIC_VER, roms, "BASIC version");
  {
    child->num_choices = 0;
    strcpy(child->choices[child->num_choices], "Auto");
    child->choice_ints[child->num_choices] = SYSROM_AUTO;
    child->num_choices++;
    for (size_t i = 0;
         i < sizeof(bmx::atari800::kBasic) / sizeof(bmx::atari800::kBasic[0]);
         i++) {
      strcpy(child->choices[child->num_choices],
             bmx::atari800::kBasic[i].name);
      child->choice_ints[child->num_choices] = bmx::atari800::kBasic[i].id;
      child->num_choices++;
    }
  }
  bmx::atari800::sync_rom_items();
}

// Blank cartridge types (CART type id + size in KB).
static const struct {
  int type;
  int kb;
} kBlankCartTypes[] = {
    {1, 8},     // Standard 8 KB
    {2, 16},    // Standard 16 KB
    {41, 128},  // AtariMax 128 KB
    {42, 1024},  // AtariMax 1 MB
};

int emux_atari_blank_cart(char *path) {
  if (path == NULL || bmx::atari800::s_cartblank_item == NULL) {
    return -1;
  }
  int sel = bmx::atari800::s_cartblank_item->value;
  if (sel < 0 || sel >= 4) {
    return -1;
  }
  int type = kBlankCartTypes[sel].type;
  int kb = kBlankCartTypes[sel].kb;
  char fixed[300];
  snprintf(fixed, sizeof(fixed), "%s", path);
  {
    const char *slash = strrchr(fixed, '/');
    const char *base = slash != NULL ? slash + 1 : fixed;
    if (strchr(base, '.') == NULL) {
      strncat(fixed, ".car", sizeof(fixed) - strlen(fixed) - 1);
    }
  }
  FILE *fp = fopen(fixed, "wb");
  if (fp == NULL) {
    return -1;
  }
  // CART header (T CART + BE type + BE checksum) + erased flash (0xFF).
  unsigned char hdr[16];
  UBYTE *image = (UBYTE *)malloc((size_t)kb * 1024U);
  bool ok = image != NULL;
  if (ok) {
    int nbytes = kb * 1024;
    int checksum;
    memset(image, 0xff, (size_t)nbytes);
    checksum = CARTRIDGE_Checksum(image, nbytes);
    hdr[0] = 'C';
    hdr[1] = 'A';
    hdr[2] = 'R';
    hdr[3] = 'T';
    hdr[4] = (unsigned char)((type >> 24) & 0xff);
    hdr[5] = (unsigned char)((type >> 16) & 0xff);
    hdr[6] = (unsigned char)((type >> 8) & 0xff);
    hdr[7] = (unsigned char)(type & 0xff);
    hdr[8] = (unsigned char)((checksum >> 24) & 0xff);
    hdr[9] = (unsigned char)((checksum >> 16) & 0xff);
    hdr[10] = (unsigned char)((checksum >> 8) & 0xff);
    hdr[11] = (unsigned char)(checksum & 0xff);
    hdr[12] = hdr[13] = hdr[14] = hdr[15] = 0;
    ok = fwrite(hdr, 1, sizeof(hdr), fp) == sizeof(hdr);
    // 1MB writes in chunks (stack is small on bare metal).
    unsigned char chunk[4096];
    memset(chunk, 0xff, sizeof(chunk));
    int left = nbytes;
    while (ok && left > 0) {
      size_t w = (size_t)left > sizeof(chunk) ? sizeof(chunk) : (size_t)left;
      ok = fwrite(chunk, 1, w, fp) == w;
      left -= (int)w;
    }
    free(image);
  }
  fclose(fp);
  return ok ? 0 : -1;
}

struct menu_item *emux_add_cartridge_options(struct menu_item *parent) {
  struct menu_item *folder =
      ui_menu_add_folder(parent, "Cartridge");
  bmx::atari800::s_cartreboot_item = NULL;
  bmx::atari800::s_cartblank_item = NULL;
  ui_menu_add_button(MENU_ATARI_ATTACH_CART, folder, "Attach cartridge...");
  ui_menu_add_button(MENU_DETACH_CART, folder, "Detach cartridge");
  bmx::atari800::s_cartreboot_item = ui_menu_add_toggle(
      MENU_ATARI_CART_REBOOT, folder, "Reboot on change",
      CARTRIDGE_autoreboot ? 1 : 0);
  struct menu_item *blanktype = bmx::atari800::s_cartblank_item =
      ui_menu_add_multiple_choice(MENU_ATARI_CART_BLANKTYPE, folder,
                                  "Blank type");
  blanktype->num_choices = 4;
  strcpy(blanktype->choices[0], "Standard 8 KB");
  blanktype->choice_ints[0] = 0;
  strcpy(blanktype->choices[1], "Standard 16 KB");
  blanktype->choice_ints[1] = 1;
  strcpy(blanktype->choices[2], "AtariMax 128 KB");
  blanktype->choice_ints[2] = 2;
  strcpy(blanktype->choices[3], "AtariMax 1 MB");
  blanktype->choice_ints[3] = 3;
  blanktype->value = 3;
  ui_menu_add_button(MENU_ATARI_CART_BLANK, folder,
                     "Make blank cartridge...");
  return folder;
}

void emux_add_userport_joys(struct menu_item *parent) {
  (void)parent;
}

void emux_create_disk(struct menu_item *item, fullpath_func f_fullpath) {
  (void)item;
  (void)f_fullpath;
}

void emux_create_tape(struct menu_item *item, fullpath_func f_fullpath) {
  (void)item;
  (void)f_fullpath;
}

struct menu_item *emux_add_palette_options(int menu_id,
                                           struct menu_item *parent) {
  (void)menu_id;
  (void)parent;
  return NULL;
}

int emux_change_palette(int display_num, int palette_index) {
  (void)display_num;
  (void)palette_index;
  return 0;
}

int emux_apply_palette_setting(int display_num) {
  (void)display_num;
  return 0;
}

int emux_set_palette_setting(int display_num, const char *setting) {
  (void)display_num;
  (void)setting;
  return 0;
}

const char *emux_get_palette_setting(int display_num) {
  (void)display_num;
  return NULL;
}

void emux_video_color_setting_changed(int display_num) {
  (void)display_num;
  ::bmx::atari800::apply_colors_to_core();
}

}  // extern "C"

namespace bmx {
namespace atari800 {

void apply_colors_to_core(void) {
  if (Colours_setup == NULL) {
    return;
  }
  Colours_setup->brightness = (s_col_bri - 1000) / 500.0;
  Colours_setup->contrast = (s_col_con - 1000) / 500.0;
  Colours_setup->saturation = (s_col_sat - 1000) / 1000.0;
  Colours_setup->hue = (s_col_tin - 1000) / 1000.0;
  double gam = (s_col_gam / 1000.0) * 2.35;
  if (gam < 1.0) {
    gam = 1.0;
  }
  if (gam > 3.5) {
    gam = 3.5;
  }
  Colours_setup->gamma = gam;
  Colours_Update();
  // The emulator pauses while the menu is open, so the frame loop's
  // palette upload stalls with it. Push the rebuilt table to the FBL
  // immediately so the canvas preview behind the menu tracks the
  // sliders live. video_refresh_palette applies the monitor transform.
  video_refresh_palette();
}

// Pushes staged sound choices into Sound_desired and re-runs Sound_Setup,
// mirroring the TUI (which re-runs setup live when freq/depth/channels
// change). Sound_Setup leaves the mixer paused, so pair it with
// Sound_Continue like sound_init does.
bool apply_sound_setup(void) {
  Sound_desired.freq = 44100;
  Sound_desired.sample_size = 2;  // 16-bit
  Sound_desired.channels = s_snd_stereo ? 2 : 1;
  Sound_desired.buffer_ms = 50;
  Sound_desired.buffer_frames = 0;  // backend decides exact frame count
  bool ok = Sound_Setup() ? true : false;
  if (ok) {
    Sound_Continue();
  }
  Sound_SetLatency((unsigned int)s_snd_latency);
  return ok;
}

}  // namespace atari800
}  // namespace bmx

extern "C" {

static int clamp_color(int value, int max) {
  if (value < 0) {
    return 0;
  }
  if (value > max) {
    return max;
  }
  return value;
}

void emux_set_color_brightness(int display_num, int value) {
  (void)display_num;
  bmx::atari800::s_col_bri = clamp_color(value, 2000);
  bmx::atari800::apply_colors_to_core();
}

void emux_set_color_contrast(int display_num, int value) {
  (void)display_num;
  bmx::atari800::s_col_con = clamp_color(value, 2000);
  bmx::atari800::apply_colors_to_core();
}

void emux_set_color_gamma(int display_num, int value) {
  (void)display_num;
  bmx::atari800::s_col_gam = clamp_color(value, 4000);
  bmx::atari800::apply_colors_to_core();
}

void emux_set_color_tint(int display_num, int value) {
  (void)display_num;
  bmx::atari800::s_col_tin = clamp_color(value, 2000);
  bmx::atari800::apply_colors_to_core();
}

void emux_set_color_saturation(int display_num, int value) {
  (void)display_num;
  bmx::atari800::s_col_sat = clamp_color(value, 2000);
  bmx::atari800::apply_colors_to_core();
}

int emux_get_color_brightness(int display_num) {
  (void)display_num;
  return bmx::atari800::s_col_bri;
}

int emux_get_color_contrast(int display_num) {
  (void)display_num;
  return bmx::atari800::s_col_con;
}

int emux_get_color_gamma(int display_num) {
  (void)display_num;
  return bmx::atari800::s_col_gam;
}

int emux_get_color_tint(int display_num) {
  (void)display_num;
  return bmx::atari800::s_col_tin;
}

int emux_get_color_saturation(int display_num) {
  (void)display_num;
  return bmx::atari800::s_col_sat;
}

void emux_get_default_color_setting(int *brightness, int *contrast,
                                    int *gamma, int *tint, int *saturation) {
  if (brightness) *brightness = 1000;
  if (contrast) *contrast = 1000;
  if (gamma) *gamma = 1000;
  if (tint) *tint = 1000;
  if (saturation) *saturation = 1000;
}

// emux_apply_video_adjustments is provided by the common library (emux_api.c).

void emux_set_video_cache(int value) {
  (void)value;
}

void emux_set_hw_scale(int value) {
  (void)value;
}

void emux_set_int(IntSetting setting, int value) {
  if (setting == Setting_WarpMode) {
    bmx::atari800::g_warp = value ? 1 : 0;
    return;
  }
  (void)value;
}

void emux_set_int_1(IntSetting setting, int value, int param) {
  (void)setting;
  (void)value;
  (void)param;
}

void emux_get_int(IntSetting setting, int *dest) {
  if (dest == NULL) {
    return;
  }
  if (setting == Setting_WarpMode) {
    *dest = bmx::atari800::g_warp;
    return;
  }
  *dest = 0;
}

void emux_get_int_1(IntSetting setting, int *dest, int param) {
  (void)setting;
  (void)param;
  if (dest != NULL) *dest = 0;
}

void emux_get_string_1(StringSetting setting, const char **dest, int param) {
  (void)setting;
  (void)param;
  if (dest != NULL) *dest = NULL;
}

int emux_save_settings(void) {
  return 0;
}

void emux_load_settings_done(void) {
  // Clamp RAM against the loaded machine model. Pre-init this only stages
  // globals; reinitialise_machine() is a no-op until config_init runs.
  apply_loaded_machine();
  // Staged sound choices reach the mixer via sound_init at boot; if settings
  // are (re)loaded mid-session, push them now (Sound_Setup is idempotent).
  if (bmx::atari800::config_is_ready()) {
    bmx::atari800::apply_sound_setup();
  }
  // Rows were built from pre-load defaults; sync them to the loaded values
  // so the menu never disagrees with the running core (e.g. BASIC).
  bmx::atari800::sync_all_menu_items();
}

void emux_load_additional_settings(void) {}

void emux_save_additional_settings(FILE *fp) {
  fprintf(fp, "atari_machine=%d\n", Atari800_machine_type);
  fprintf(fp, "atari_ram=%d\n", MEMORY_ram_size);
  fprintf(fp, "atari_game=%d\n", Atari800_builtin_game ? 1 : 0);
  fprintf(fp, "atari_os800=%d\n",
          SYSROM_os_versions[Atari800_MACHINE_800]);
  fprintf(fp, "atari_osxlxe=%d\n",
          SYSROM_os_versions[Atari800_MACHINE_XLXE]);
  fprintf(fp, "atari_os5200=%d\n",
          SYSROM_os_versions[Atari800_MACHINE_5200]);
  fprintf(fp, "atari_basicver=%d\n", SYSROM_basic_version);
  fprintf(fp, "atari_basic=%d\n", Atari800_disable_basic == 0 ? 1 : 0);
  fprintf(fp, "atari_sio=%d\n", libatari800_get_sio_patch_enabled() ? 1 : 0);
  fprintf(fp, "atari_artif=%d\n", ANTIC_artif_mode);
  fprintf(fp, "atari_sound=%d\n", Sound_enabled ? 1 : 0);
  fprintf(fp, "atari_monitor=%d\n", bmx::atari800::video_get_monitor());
  fprintf(fp, "atari_bri=%d\n", bmx::atari800::s_col_bri);
  fprintf(fp, "atari_con=%d\n", bmx::atari800::s_col_con);
  fprintf(fp, "atari_sat=%d\n", bmx::atari800::s_col_sat);
  fprintf(fp, "atari_tin=%d\n", bmx::atari800::s_col_tin);
  fprintf(fp, "atari_gam=%d\n", bmx::atari800::s_col_gam);
  fprintf(fp, "atari_leddisk=%d\n", Screen_show_disk_led ? 1 : 0);
  fprintf(fp, "atari_ledsector=%d\n", Screen_show_sector_counter ? 1 : 0);
  fprintf(fp, "atari_led1200=%d\n", Screen_show_1200_leds ? 1 : 0);
  fprintf(fp, "atari_speed=%d\n", Screen_show_atari_speed ? 1 : 0);
  fprintf(fp, "atari_stereo=%d\n", bmx::atari800::s_snd_stereo ? 1 : 0);
  fprintf(fp, "atari_hifi=%d\n", POKEYSND_enable_new_pokey ? 1 : 0);
  fprintf(fp, "atari_click=%d\n", POKEYSND_console_sound_enabled ? 1 : 0);
  fprintf(fp, "atari_bienias=%d\n", POKEYSND_bienias_fix ? 1 : 0);
  fprintf(fp, "atari_latency=%d\n", bmx::atari800::s_snd_latency);
  fprintf(fp, "atari_rtime=%d\n", RTIME_enabled ? 1 : 0);
  fprintf(fp, "atari_slowboot=%d\n", BINLOAD_slow_xex_loading ? 1 : 0);
  fprintf(fp, "atari_cartreboot=%d\n", CARTRIDGE_autoreboot ? 1 : 0);
  for (int i = 0; i < 6; i++) {
    fprintf(fp, "atari_f%d=%d\n", 6 + i, bmx::atari800::g_fkey_action[i]);
  }
}

static void apply_loaded_machine(void) {
  if (!bmx::atari800::ram_valid_for_machine(Atari800_machine_type,
                                            MEMORY_ram_size)) {
    MEMORY_ram_size =
        bmx::atari800::default_ram_for_machine(Atari800_machine_type);
  }
  // Pre-init (settings load) only stages globals; live changes reinit.
  bmx::atari800::reinitialise_machine();
}

int emux_handle_loaded_setting(char *name, char *value_str, int value) {
  (void)value_str;
  if (strcmp(name, "atari_machine") == 0) {
    if (value >= Atari800_MACHINE_800 && value < Atari800_MACHINE_SIZE) {
      Atari800_machine_type = value;
    }
    return 1;
  }
  if (strcmp(name, "atari_ram") == 0) {
    MEMORY_ram_size = value;
    return 1;
  }
  if (strcmp(name, "atari_basic") == 0) {
    Atari800_disable_basic = value ? 0 : 1;
    bmx::atari800::g_basic_pref_loaded = true;
    return 1;
  }
  if (strcmp(name, "atari_sio") == 0) {
    libatari800_set_sio_patch_enabled(value ? 1 : 0);
    return 1;
  }
  if (strcmp(name, "atari_artif") == 0) {
    if (value >= 0 && value <= 4) {
      ANTIC_artif_mode = value;
      ANTIC_UpdateArtifacting();
    }
    return 1;
  }
  if (strcmp(name, "atari_sound") == 0) {
    Sound_enabled = value ? 1 : 0;
    return 1;
  }
  if (strcmp(name, "atari_game") == 0) {
    Atari800_builtin_game = value ? TRUE : FALSE;
    return 1;
  }
  if (strcmp(name, "atari_os800") == 0) {
    if (value >= 0 && value <= SYSROM_SIZE) {
      SYSROM_os_versions[Atari800_MACHINE_800] = value;
    }
    return 1;
  }
  if (strcmp(name, "atari_osxlxe") == 0) {
    if (value >= 0 && value <= SYSROM_SIZE) {
      SYSROM_os_versions[Atari800_MACHINE_XLXE] = value;
    }
    return 1;
  }
  if (strcmp(name, "atari_os5200") == 0) {
    if (value >= 0 && value <= SYSROM_SIZE) {
      SYSROM_os_versions[Atari800_MACHINE_5200] = value;
    }
    return 1;
  }
  if (strcmp(name, "atari_basicver") == 0) {
    if (value >= 0 && value <= SYSROM_SIZE) {
      SYSROM_basic_version = value;
    }
    return 1;
  }
  if (strcmp(name, "atari_monitor") == 0) {
    bmx::atari800::video_set_monitor(value);
    return 1;
  }
  if (strcmp(name, "atari_bri") == 0) {
    bmx::atari800::s_col_bri = value < 0 ? 0 : (value > 2000 ? 2000 : value);
    return 1;
  }
  if (strcmp(name, "atari_con") == 0) {
    bmx::atari800::s_col_con = value < 0 ? 0 : (value > 2000 ? 2000 : value);
    return 1;
  }
  if (strcmp(name, "atari_sat") == 0) {
    bmx::atari800::s_col_sat = value < 0 ? 0 : (value > 2000 ? 2000 : value);
    return 1;
  }
  if (strcmp(name, "atari_tin") == 0) {
    bmx::atari800::s_col_tin = value < 0 ? 0 : (value > 2000 ? 2000 : value);
    return 1;
  }
  if (strcmp(name, "atari_gam") == 0) {
    bmx::atari800::s_col_gam = value < 0 ? 0 : (value > 4000 ? 4000 : value);
    return 1;
  }
  if (strcmp(name, "atari_leddisk") == 0) {
    Screen_show_disk_led = value ? 1 : 0;
    return 1;
  }
  if (strcmp(name, "atari_ledsector") == 0) {
    Screen_show_sector_counter = value ? 1 : 0;
    return 1;
  }
  if (strcmp(name, "atari_led1200") == 0) {
    Screen_show_1200_leds = value ? 1 : 0;
    return 1;
  }
  if (strcmp(name, "atari_speed") == 0) {
    Screen_show_atari_speed = value ? 1 : 0;
    return 1;
  }
  if (strcmp(name, "atari_stereo") == 0) {
    bmx::atari800::s_snd_stereo = value ? 1 : 0;
    return 1;
  }
  if (strcmp(name, "atari_hifi") == 0) {
    POKEYSND_enable_new_pokey = value ? 1 : 0;
    return 1;
  }
  if (strcmp(name, "atari_click") == 0) {
    POKEYSND_console_sound_enabled = value ? 1 : 0;
    return 1;
  }
  if (strcmp(name, "atari_bienias") == 0) {
    POKEYSND_bienias_fix = value ? 1 : 0;
    return 1;
  }
  if (strcmp(name, "atari_latency") == 0) {
    if (value < 0) {
      value = 0;
    }
    if (value > 100) {
      value = 100;
    }
    bmx::atari800::s_snd_latency = value;
    return 1;
  }
  if (strcmp(name, "atari_rtime") == 0) {
    RTIME_enabled = value ? 1 : 0;
    return 1;
  }
  if (strcmp(name, "atari_slowboot") == 0) {
    BINLOAD_slow_xex_loading = value ? 1 : 0;
    return 1;
  }
  if (strcmp(name, "atari_cartreboot") == 0) {
    CARTRIDGE_autoreboot = value ? 1 : 0;
    return 1;
  }
  if (strncmp(name, "atari_f", 7) == 0 && name[7] >= '6' && name[7] <= '9' &&
      name[8] == '\0') {
    int slot = name[7] - '6';
    bmx::atari800::g_fkey_action[slot] =
        (value < 0 || value > 6) ? 0 : value;
    return 1;
  }
  if (strcmp(name, "atari_f10") == 0 || strcmp(name, "atari_f11") == 0) {
    int slot = name[8] == '0' ? 4 : 5;
    bmx::atari800::g_fkey_action[slot] =
        (value < 0 || value > 6) ? 0 : value;
    return 1;
  }
  return 0;
}

int emux_handle_menu_change(struct menu_item *item) {
  if (item->id >= MENU_ATARI_F6 && item->id <= MENU_ATARI_F11) {
    int slot = item->id - MENU_ATARI_F6;
    int action = item->value;
    if (action < 0 || action > 6) {
      action = 0;
    }
    bmx::atari800::g_fkey_action[slot] = action;
    if (bmx::atari800::s_fkey_items[slot] != NULL) {
      bmx::atari800::s_fkey_items[slot]->value = action;
    }
    return 1;
  }
  switch (item->id) {
  case MENU_ATARI_MACHINE: {
    int sub = item->choice_ints[item->value];
    bmx::atari800::apply_submodel(sub);
    bmx::atari800::sync_ram_item();
    bmx::atari800::sync_machine_item();
    return 1;
  }
  case MENU_ATARI_RAM: {
    int ram = item->choice_ints[item->value];
    if (!bmx::atari800::ram_valid_for_machine(Atari800_machine_type, ram)) {
      ram = bmx::atari800::default_ram_for_machine(Atari800_machine_type);
    }
    MEMORY_ram_size = ram;
    bmx::atari800::reinitialise_machine();
    bmx::atari800::sync_ram_item();
    return 1;
  }
  case MENU_ATARI_BASIC:
    Atari800_disable_basic = item->value ? 0 : 1;
    bmx::atari800::reinitialise_machine();
    return 1;
  case MENU_ATARI_SIO_PATCH:
    libatari800_set_sio_patch_enabled(item->value ? 1 : 0);
    return 1;
  case MENU_ATARI_RTIME:
    RTIME_enabled = item->value ? 1 : 0;
    return 1;
  case MENU_ATARI_SLOWBOOT:
    BINLOAD_slow_xex_loading = item->value ? 1 : 0;
    return 1;
  case MENU_ATARI_ROTATE:
    SIO_RotateDisks();
    retrack_all_drives();
    bmx::atari800::sync_drive_items();
    return 1;
  case MENU_ATARI_CART_REBOOT:
    CARTRIDGE_autoreboot = item->value ? 1 : 0;
    return 1;
  case MENU_ATARI_CPUSTATE: {
    // Snapshot CPU/ANTIC state into the log (same source as the frame
    // fault path). Shows where a hung program actually sits.
    emulator_state_t *st =
        (emulator_state_t *)malloc(sizeof(emulator_state_t));
    if (st == NULL) {
      return 1;
    }
    libatari800_get_current_state(st);
    const cpu_state_t *cpu =
        (const cpu_state_t *)&st->state[st->tags.cpu];
    const pc_state_t *pc = (const pc_state_t *)&st->state[st->tags.pc];
    bmx::atari800::debug_log(
        "Atari800 state: PC=%04x A=%02x X=%02x Y=%02x P=%02x S=%02x "
        "dlist=%04x frames=%d",
        pc->PC, cpu->A, cpu->X, cpu->Y, cpu->P, cpu->S,
        (unsigned)ANTIC_dlist, libatari800_get_frame_number());
    free(st);
    // Core register state exactly as the program reads it (PORTA sticks,
    // STRIG fire, consol keys, KBCODE last key). NOTE: MEMORY_mem does NOT
    // back $D0xx/$D2xx/$D3xx (I/O area); read the core variables instead.
    bmx::atari800::debug_log(
        "Atari800 hw: PORTA=%02x STRIG0=%02x consol=%02x KBCODE=%02x "
        "keycode=%d",
        PIA_PORT_input[0], GTIA_TRIG[0], INPUT_key_consol, POKEY_KBCODE,
        INPUT_key_code);
    return 1;
  }
  case MENU_ATARI_ARTIFACT:
    ANTIC_artif_mode = item->choice_ints[item->value];
    ANTIC_UpdateArtifacting();
    return 1;
  case MENU_ATARI_MONITOR:
    bmx::atari800::video_set_monitor(item->choice_ints[item->value]);
    bmx::atari800::apply_colors_to_core();
    return 1;
  case MENU_ATARI_SOUND:
    Sound_enabled = item->value ? 1 : 0;
    return 1;
  case MENU_ATARI_STEREO:
    // Dual POKEY needs a cold restart (POKEY switch restriction, same as
    // the TUI which reboots here): re-setup channels, then reboot.
    bmx::atari800::s_snd_stereo = item->value ? 1 : 0;
    bmx::atari800::apply_sound_setup();
    bmx::atari800::reinitialise_machine();
    return 1;
  case MENU_ATARI_HIFI:
    // POKEY engine switch, same reboot requirement as the TUI.
    POKEYSND_enable_new_pokey = item->value ? 1 : 0;
    POKEYSND_DoInit();
    bmx::atari800::reinitialise_machine();
    return 1;
  case MENU_ATARI_KEYCLICK:
    POKEYSND_console_sound_enabled = item->value ? 1 : 0;
    return 1;
  case MENU_ATARI_BIENIAS:
    POKEYSND_bienias_fix = item->value ? 1 : 0;
    return 1;
  case MENU_ATARI_LATENCY:
    bmx::atari800::s_snd_latency = item->value;
    if (bmx::atari800::s_snd_latency < 0) {
      bmx::atari800::s_snd_latency = 0;
    }
    if (bmx::atari800::s_snd_latency > 100) {
      bmx::atari800::s_snd_latency = 100;
    }
    Sound_SetLatency((unsigned int)bmx::atari800::s_snd_latency);
    return 1;
  case MENU_ATARI_ROM_RESCAN: {
    // TUI parity: report how many ROM slots the scan filled, like the
    // desktop "found X system roms" feedback.
    bmx::atari800::rescan_rom_pool();
    bmx::atari800::sync_rom_items();
    char found[64];
    snprintf(found, sizeof(found), "Found %d system ROMs",
             bmx::atari800::last_rom_pool_matches());
    ui_info(found);
    return 1;
  }
  case MENU_ATARI_OS_VER: {
    int pref = item->choice_ints[item->value];
    SYSROM_os_versions[Atari800_machine_type] = pref;
    SYSROM_os_versions[Atari800_machine_type] =
        bmx::atari800::clamp_os_pref(pref);
    bmx::atari800::reinitialise_machine();
    bmx::atari800::sync_rom_items();
    return 1;
  }
  case MENU_ATARI_BASIC_VER: {
    int pref = item->choice_ints[item->value];
    SYSROM_basic_version = pref;
    SYSROM_basic_version = bmx::atari800::clamp_basic_pref(pref);
    bmx::atari800::reinitialise_machine();
    bmx::atari800::sync_rom_items();
    return 1;
  }
  case MENU_ATARI_LED_DISK:
    Screen_show_disk_led = item->value ? 1 : 0;
    return 1;
  case MENU_ATARI_LED_SECTOR:
    Screen_show_sector_counter = item->value ? 1 : 0;
    return 1;
  case MENU_ATARI_LED_1200:
    Screen_show_1200_leds = item->value ? 1 : 0;
    return 1;
  case MENU_ATARI_SPEED:
    Screen_show_atari_speed = item->value ? 1 : 0;
    return 1;
  case MENU_ATARI_SCREENSHOT: {
    // Auto-named BMP dump of the emulator frame (same volume preference
    // as snapshot saves: USB stick first, then SD system volume).
    static const char *kShotDirs[] = {
        "USER:/snapshots/ATARI800",
        "SYS:/snapshots/ATARI800",
        "SD:/snapshots/ATARI800",
    };
    char path[64];
    bool saved = false;
    for (size_t d = 0;
         d < sizeof(kShotDirs) / sizeof(kShotDirs[0]) && !saved; d++) {
      for (int n = 0; n < 100 && !saved; n++) {
        snprintf(path, sizeof(path), "%s/shot%02d.bmp", kShotDirs[d], n);
        FILE *probe = fopen(path, "rb");
        if (probe != NULL) {
          fclose(probe);
          continue;  // name taken
        }
        if (bmx::atari800::video_save_screenshot(path)) {
          saved = true;
          char msg[64];
          snprintf(msg, sizeof(msg), "Saved shot%02d.bmp", n);
          ui_info(msg);
        } else if (n == 0) {
          break;  // dir itself unwritable, try next volume
        }
      }
    }
    if (!saved) {
      ui_info("Screenshot failed");
    }
    return 1;
  }
  default:
    break;
  }
  if (item->id >= MENU_ATARI_EJECT_1 && item->id <= MENU_ATARI_EJECT_8) {
    int drive = item->id - MENU_ATARI_EJECT_1 + 1;
    libatari800_unmount_disk(drive);
    bmx::atari800::track_disk_name(drive, NULL);
    bmx::atari800::sync_drive_items();
    return 1;
  }
  if (item->id >= MENU_ATARI_RO_1 && item->id <= MENU_ATARI_RO_8) {
    int drive = item->id - MENU_ATARI_RO_1 + 1;
    int st = SIO_drive_status[drive - 1];
    if (st == SIO_READ_WRITE || st == SIO_READ_ONLY) {
      int ro = (st == SIO_READ_WRITE) ? 1 : 0;
      if (libatari800_mount_disk(drive, SIO_filename[drive - 1], ro)) {
        bmx::atari800::track_disk_name(drive, SIO_filename[drive - 1]);
      }
    }
    bmx::atari800::sync_drive_items();
    return 1;
  }
  return 0;
}

int emux_handle_quick_func(int button_func, fullpath_func f_fullpath) {
  (void)button_func;
  (void)f_fullpath;
  return 0;
}

int emux_handle_rom_change(struct menu_item *item, fullpath_func f_fullpath) {
  (void)item;
  (void)f_fullpath;
  return 0;
}

// -- video ensure / easyflash ----------------------------------------------
// emux_ensure_video is provided by the common library (emux_api.c).
// emux_enable_drive_status / emux_display_* are provided by overlay.c.

void emux_vice_easy_flash(void) {}

// -- network / developer / api ---------------------------------------------
// NOTE: emux_network_is_ready, emux_get_network_addresses, emux_wifi_* are
// provided by src/network/network_service.cpp (included in ATARI800 builds).
// emux_update_*_explicit are provided by src/update/{update_service,
// menu_update_progress_bridge}.cpp. Do not redefine them here.

int emux_developer_mode_enabled(void) {
  return 0;
}

int emux_get_developer_password(char *password, unsigned password_size) {
  (void)password;
  (void)password_size;
  return 0;
}

unsigned emux_get_developer_log_buffer_kb(void) {
  return 0;
}

int emux_api_mode_enabled(void) {
  return 0;
}

int emux_get_api_password(char *password, unsigned password_size) {
  (void)password;
  (void)password_size;
  return 0;
}

// -- RS232 / userport ------------------------------------------------------

int emux_apply_rs232net(int enabled, int mode, int interface,
                        const char *target, int baud, int ip232,
                        int hayes_audio, const char *phonebook) {
  (void)enabled;
  (void)mode;
  (void)interface;
  (void)target;
  (void)baud;
  (void)ip232;
  (void)hayes_audio;
  (void)phonebook;
  return 0;
}

void emux_set_rs232_hayes_audio(int hayes_audio) {
  (void)hayes_audio;
}

// -- VICE-only link stubs ----------------------------------------------------
// The ATARI800 kernel links shared BMX objects (async_network, kernel GPIO /
// userport paths, common menu/ui listing helpers) that reference VICE
// symbols on VICE machines. The atari800 core has no equivalent, so provide
// safe no-op stubs here. These intentionally return failure/empty so callers
// degrade gracefully instead of crashing.

// vicesocket (async_network.cpp)
typedef struct vice_network_socket_s vice_network_socket_t;
typedef struct vice_network_socket_address_s vice_network_socket_address_t;

vice_network_socket_t *vice_network_client(
    const vice_network_socket_address_t *server_address) {
  (void)server_address;
  return NULL;
}

vice_network_socket_address_t *vice_network_address_generate(const char *address,
                                                             unsigned short port) {
  (void)address;
  (void)port;
  return NULL;
}

void vice_network_address_close(vice_network_socket_address_t *addr) {
  (void)addr;
}

int vice_network_socket_close(vice_network_socket_t *sockfd) {
  (void)sockfd;
  return -1;
}

ssize_t vice_network_send(vice_network_socket_t *sockfd, const void *buffer,
                          size_t buffer_length, int flags) {
  (void)sockfd;
  (void)buffer;
  (void)buffer_length;
  (void)flags;
  return -1;
}

ssize_t vice_network_receive(vice_network_socket_t *sockfd, void *buffer,
                             size_t buffer_length, int flags) {
  (void)sockfd;
  (void)buffer;
  (void)buffer_length;
  (void)flags;
  return -1;
}

int vice_network_select_poll_one(vice_network_socket_t *readsockfd) {
  (void)readsockfd;
  return -1;
}

int vice_network_get_errorcode(void) {
  return -1;
}

// userport (kernel.cpp GPIO paths; atari has no userport CIA)
uint8_t circle_get_userport_ddr(void) {
  return 0;
}

uint8_t circle_get_userport(void) {
  return 0xff;
}

void circle_set_userport(uint8_t value) {
  (void)value;
}

// monitor peek (kernel debug path)
uint8_t mem_bank_peek(int bank, uint16_t addr, void *context) {
  (void)bank;
  (void)addr;
  (void)context;
  return 0;
}

// disk/tape image listing (common menu.c)
struct image_contents_s;

struct image_contents_s *diskcontents_filesystem_read(const char *file_name) {
  (void)file_name;
  return NULL;
}

struct image_contents_s *tapecontents_read(const char *file_name) {
  (void)file_name;
  return NULL;
}

typedef struct image_contents_s image_contents_t;
typedef struct image_contents_file_list_s image_contents_file_list_t;

void image_contents_destroy(image_contents_t *contents) {
  (void)contents;
}

char *image_contents_to_string(image_contents_t *contents, char out_charset) {
  (void)contents;
  (void)out_charset;
  return NULL;
}

char *image_contents_file_to_string(image_contents_file_list_t *p,
                                    char out_charset) {
  (void)p;
  (void)out_charset;
  return NULL;
}

// PETSCII helpers (common ui.c; atari uses ATASCII, no conversion)
uint8_t charset_petscii_to_screencode(uint8_t code, unsigned int reverse_mode) {
  (void)reverse_mode;
  return code;
}

void vsync_suspend_speed_eval(void) {
  bmx::atari800::debug_log("Atari800: menu closed");
}

}  // extern "C"
