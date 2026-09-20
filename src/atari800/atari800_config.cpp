//
// atari800_config.cpp
//
// Atari800 core configuration for BMX bare-metal.
//
// atari800 (via libatari800) initializes from an argv list, exactly like the
// standalone program. We build a minimal argv that selects the TV mode and
// machine model, then call libatari800_init(). With EMUOS_ALTIRRA the Altirra
// OS and BASIC are compiled in as zero-config fallbacks; after init we scan
// /roms (staged 1:1 from bmx/roms/a800/) with atari800's own CRC32/size
// database, so every machine model finds its ROMs however they are named.
// Explicit /atari/*.rom files and menu settings take precedence.
//



#include "atari800_config.h"
#include "atari800_debug.h"

extern "C" {
#include "third_party/atari800/src/config.h"
#include "third_party/atari800/src/atari.h"
#include "third_party/atari800/src/crc32.h"
#include "third_party/atari800/src/memory.h"
#include "third_party/atari800/src/sound.h"
#include "third_party/atari800/src/sysrom.h"
#include "third_party/atari800/src/libatari800/libatari800.h"
}

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#else
struct dirent;
#endif

namespace bmx {
namespace atari800 {

static bool s_ntsc = false;
static bool s_ready = false;

// Canonical SD locations for optional external ROMs. When present they
// override the compiled-in Altirra OS/BASIC via the core's own -osb_rom /
// -xlxe_rom / -5200_rom / -basic_rom options (same names the desktop SDL
// port documents). Absent files simply keep the zero-config Altirra boot.
struct RomProbe {
  const char *option;
  const char *path;
};

static const RomProbe kRomProbes[] = {
    {"-osb_rom", "/atari/atariosb.rom"},
    {"-xlxe_rom", "/atari/atarixl.rom"},
    {"-5200_rom", "/atari/atari5200.rom"},
    {"-basic_rom", "/atari/ataribasic.rom"},
};



bool config_init(const Config &config) {
  s_ntsc = (config.tv_mode != nullptr)
               ? (strcmp(config.tv_mode, "-ntsc") == 0)
               : false;
  const char *tv_arg = s_ntsc ? "-ntsc" : "-pal";

  // Default to the 800/XL/XE family, which the Altirra OS supports.
  // NOTE: argc counts only real args. Upstream disables BASIC by default
  // (Atari800_disable_basic=TRUE); the desktop port turns it back on via
  // config, so pass -basic here unless the menu settings staged an
  // explicit preference (loaded before the emulator launches).
  const char *argv[2 + 1 + 2 * 4 + 1];
  int argc = 0;
  argv[argc++] = "atari800";
  argv[argc++] = tv_arg;
  if (!basic_preference_loaded()) {
    argv[argc++] = "-basic";
  }
  for (size_t i = 0; i < sizeof(kRomProbes) / sizeof(kRomProbes[0]); i++) {
    FILE *probe = fopen(kRomProbes[i].path, "rb");
    if (probe == NULL) {
      continue;
    }
    fclose(probe);
    argv[argc++] = kRomProbes[i].option;
    argv[argc++] = kRomProbes[i].path;
  }
  argv[argc] = NULL;

  if (!libatari800_init(argc, (char **)argv)) {
    return false;
  }
  s_ready = true;

  // rescan_rom_pool() already reboots when it picks up new ROMs; only
  // cold-start here when it had nothing to activate.
  bool rebooted = rescan_rom_pool();
  clamp_all_rom_prefs();
  apply_staged_colors();

  // libatari800_init initialises the hardware but leaves the CPU in a
  // zeroed power-on state; without an explicit cold start the first frame
  // faults with "invalid display list". Every upstream port cold-starts
  // here (cf. atari_x11.c), so do the same (skipped when the rescan
  // above already rebooted into freshly found ROMs).
  debug_log("Atari800: machine=%d ram=%d tv=%d os_ver=%d basic=%d "
            "nobasic=%d prefbasic=%d os_magic=%02x%02x%02x%02x",
            Atari800_machine_type, MEMORY_ram_size, Atari800_tv_mode,
            Atari800_os_version, MEMORY_have_basic, Atari800_disable_basic,
            basic_preference_loaded() ? 1 : 0, MEMORY_os[0], MEMORY_os[1],
            MEMORY_os[2], MEMORY_os[3]);
  if (!rebooted) {
    debug_log("Atari800: cold-starting machine");
    Atari800_Coldstart();
  }
  return true;
}

// Candidate locations for the staged ROM pool, in probe order. Bare
// absolute paths match the rest of BMX (bootstat, /atari probes); the
// drive-prefixed forms cover newlib leaders that require an explicit
// logical drive.
static const char *kRomPoolDirs[] = {
    "/roms",
    "SD:/roms",
    "SYS:/roms",
};

static bool dir_readable(const char *path) {
#ifdef HAVE_DIRENT_H
  DIR *dir = opendir(path);
  if (dir == NULL) {
    return false;
  }
  closedir(dir);
  return true;
#else
  (void)path;
  return false;
#endif
}

// Currently active OS/BASIC choices (resolved preference, -1 if none).
static void current_os_basic(int *os_ver, int *basic_ver) {
  int xe_ver = 0;
  SYSROM_ChooseROMs(Atari800_machine_type, MEMORY_ram_size, Atari800_tv_mode,
                    os_ver, basic_ver, &xe_ver);
}

static int s_last_pool_matches = 0;

int last_rom_pool_matches(void) {
  return s_last_pool_matches;
}

// Diagnostic walk mirroring SYSROM_FindInDir's steps with logging, to find
// why on-device matching fails. Counts entries, fopen successes and
// size-allowed files (with their CRCs).
static int count_filled_slots(void) {
  int filled = 0;
  for (int id = 0; id < SYSROM_LOADABLE_SIZE; id++) {
    if (SYSROM_roms[id].data != NULL || SYSROM_roms[id].filename[0] != '\0') {
      filled++;
    }
  }
  return filled;
}

static void debug_probe_pool(const char *pool) {
  int entries = 0, opened = 0, sized = 0;
  int filled_before = count_filled_slots();
  DIR *dir = opendir(pool);
  if (dir == NULL) {
    debug_log("Atari800 ROM scan: opendir %s FAILED", pool);
    return;
  }
  struct dirent *entry = NULL;
  char full[FILENAME_MAX];
  while ((entry = readdir(dir)) != NULL && entries < 500) {
    entries++;
    size_t dl = strlen(pool);
    size_t nl = strlen(entry->d_name);
    if (dl + 1 + nl >= sizeof(full)) {
      continue;
    }
    memcpy(full, pool, dl);
    full[dl] = '/';
    memcpy(full + dl + 1, entry->d_name, nl + 1);
    FILE *fp = fopen(full, "rb");
    if (fp == NULL) {
      if (entries <= 5) {
        debug_log("Atari800 ROM scan: [%d] '%s' fopen FAILED", entries,
                  entry->d_name);
      }
      continue;
    }
    opened++;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    if (len == 0x2800 || len == 0x4000 || len == 0x0800 || len == 0x2000) {
      sized++;
      if (sized <= 8) {
        rewind(fp);
        ULONG crc = 0;
        int ok = CRC32_FromFile(fp, &crc);
        debug_log("Atari800 ROM scan: '%s' len=%ld crc=%08x (%s)",
                  entry->d_name, len, (unsigned)crc, ok ? "ok" : "CRC-FAIL");
      }
    } else if (entries <= 5) {
      debug_log("Atari800 ROM scan: [%d] '%s' len=%ld (skip)", entries,
                entry->d_name, len);
    }
    fclose(fp);
  }
  closedir(dir);
  debug_log("Atari800 ROM scan: entries=%d opened=%d sized=%d slots=%d",
            entries, opened, sized, filled_before);
}

// Rescans the staged /roms pool at runtime (menu "Scan /roms for ROMs").
// Reinitialises + reboots only when the active OS/BASIC selection actually
// changes, mirroring the SDL TUI's System ROM settings flow. Returns true
// when a reboot was triggered.
bool rescan_rom_pool(void) {
#ifdef HAVE_DIRENT_H
  const char *pool = NULL;
  for (size_t i = 0; i < sizeof(kRomPoolDirs) / sizeof(kRomPoolDirs[0]); i++) {
    if (dir_readable(kRomPoolDirs[i])) {
      pool = kRomPoolDirs[i];
      break;
    }
  }
  if (pool == NULL) {
    debug_log("Atari800: no /roms pool on SD, Altirra built-ins active");
    return false;
  }
  int old_os = 0, old_basic = 0, new_os = 0, new_basic = 0;
  current_os_basic(&old_os, &old_basic);
  debug_probe_pool(pool);
  // Pass FALSE like the desktop TUI's "Find ROM images in a directory":
  // SYSROM_SetDefaults() clears every unset flag during init, so TRUE
  // early-returns without scanning anything. FALSE matches by CRC32/size
  // into any slot; explicit /atari/*.rom argv selections simply match
  // first and stay authoritative.
  SYSROM_FindInDir(pool, FALSE);
  s_last_pool_matches = count_filled_slots();
  debug_log("Atari800 ROM scan: slots after FindInDir=%d", s_last_pool_matches);
  current_os_basic(&new_os, &new_basic);
  if (new_os != old_os || new_basic != old_basic) {
    debug_log("Atari800: /roms scan changed OS %d->%d BASIC %d->%d, "
              "reloading",
              old_os, new_os, old_basic, new_basic);
    Atari800_InitialiseMachine();
    Atari800_Coldstart();
    return true;
  }
  debug_log("Atari800: ROM pool %s scanned, no change", pool);
  return false;
#else
  printf("Atari800: ROM pool scan disabled, Altirra built-ins active\n");
  return false;
#endif
}

bool config_is_ntsc(void) { return s_ntsc; }

bool config_is_ready(void) { return s_ready; }

}  // namespace atari800
}  // namespace bmx
