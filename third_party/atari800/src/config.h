/* config.h - Generated for BMX bare-metal build, atari800 7.2.1.
 *
 * Mirrors what upstream's own configure produces for --target=libatari800
 * (verified against a scratch configure run), minus features the bare-metal
 * port does not use (network, video/audio recording, screenshots).
 *
 * Rule: this file plus videomode.h are the ONLY BMX-specific files inside
 * third_party/atari800. Everything else stays 1:1 with upstream.
 *
 * Deliberately NOT defined: PBI_XLD (object not compiled), NETSIO and
 * R-device support (no network), VIDEO/AUDIO_RECORDING and SCREENSHOTS
 * SCREENSHOTS (BMX has its own screenshot path), VERY_SLOW/CRASH_MENU/
 * PAGED_ATTRIB/CYCLES_PER_OPCODE (upstream defaults off).
 */
#ifndef CONFIG_H_
#define CONFIG_H_

#define PACKAGE_NAME "atari800"
#define PACKAGE_VERSION "7.2.1"
#define PACKAGE_STRING "atari800 7.2.1"
#define VERSION "7.2.1"

/* Target: atari800 as a library */
#define LIBATARI800 1
#define SUPPORTS_PLATFORM_CONFIGURE 1
#define SUPPORTS_PLATFORM_TIME 1
#define BUFFERED_LOG 1

/* EmuOS Altirra ROMs compiled in */
#define EMUOS_ALTIRRA 1

/* Enable features (upstream libatari800 defaults) */
#define IDE 1
#define POKEYREC 1
#define PBI_BB 1
#define PBI_MIO 1

/* Mid-scanline GTIA register changes (COLOR/COLPM flush). Upstream enables
   this by default on every desktop target ("--enable-newcycleexact",
   default=ON); only the libatari800 target needs it spelled out. Without it
   the core keeps rendering each scanline with stale register values, which
   shows as rightward color smears on cycle-counted kernels (e.g. the MAFIA
   RastaConverter title). All stock desktop builds ship with this ON, which
   is why they never show the defect. Requires cycle_map.o (compiled). */
#define NEW_CYCLE_EXACT 1

/* Bare-metal runtime support (Circle newlib provides clock()/time.h). */
#define HAVE_SETJMP 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_TIME_H 1
#define HAVE_CLOCK 1

/* Circle's newlib provides strcasecmp() via <strings.h>. */
#define HAVE_STRINGS_H 1
#define HAVE_STRCASECMP 1

/* Circle's newlib provides opendir()/readdir() backed by FatFS (the BMX
   menu file browser uses them). Enables SYSROM_FindInDir so the kernel can
   discover ROMs in /roms by CRC32/size at boot. */
#define HAVE_DIRENT_H 1

/* No Unix-specific features */
/* #undef HAVE_UNISTD_H */
/* #undef HAVE_SYSTEM */
/* #undef HAVE_STRNCPY */
/* #undef HAVE_REWIND */
/* #undef HAVE_TMPFILE */
/* #undef HAVE_WINDOWS_H */
/* #undef HAVE_SIGNAL_H */
/* #undef HAVE_UNIXIO_H */
/* #undef HAVE_FILE_H */
/* #undef HAVE_LIBZ */
/* #undef HAVE_MMAP */

/* Sound (upstream libatari800 defaults: interpolation on, stereo on,
   console sound on) */
#define SOUND 1
#define INTERPOLATE_SOUND 1
#define STEREO_SOUND 1
#define CONSOLE_SOUND 1
/* #undef SOUND_CALLBACK */

/* Endian - ARM is little-endian */
/* #undef WORDS_BIGENDIAN */

#endif /* CONFIG_H_ */
