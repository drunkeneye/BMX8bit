/*
 * archdep.c
 *
 * Written by
 *  Randy Rossi <randy.rossi@gmail.com>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unistd.h>

#include "archdep.h"
#include "archdep_tick.h"
#include "circle.h"
#include "lib.h"
#include "log.h"
#include "machine.h"
#include "keyboard.h"
#include "keymap.h"
#include "util.h"

static const char *illegal_name_tokens = "/";
static char *default_path;
static char *program_name = NULL;

static int archdep_write_all(int fd, const char *buf, size_t len) {
  while (len > 0) {
    ssize_t written = write(fd, buf, len);
    if (written <= 0) {
      return -1;
    }
    buf += written;
    len -= written;
  }

  return 0;
}

int archdep_init(int *argc, char **argv) { return 0; }

const char *archdep_program_name(void) {
  if (program_name == NULL) {
    program_name = lib_malloc(5);
    strcpy(program_name, "vice");
  }

  return program_name;
}

const char *archdep_boot_path(void) { return ""; }

char *archdep_default_sysfile_pathlist(const char *emu_id) {

  if (default_path == NULL) {
    default_path = util_concat(archdep_boot_path(), "/", NULL);
  }

  return default_path;
}

/* Return a malloc'ed backup file name for file `fname'.  */
char *archdep_make_backup_filename(const char *fname) {
  char *tmp;

  tmp = util_concat(fname, NULL);
  tmp[strlen(tmp) - 1] = '~';
  return tmp;
}

char *archdep_default_fliplist_file_name(void) {
  static char *fname;

  lib_free(fname);
  fname = util_concat(archdep_boot_path(), "/fliplist-", machine_get_name(),
                      ".vfl", NULL);
  return fname;
}

char *archdep_default_rtc_file_name(void) {
  static char *fname;

  lib_free(fname);
  fname = util_concat(archdep_boot_path(), "/vice.rtc", NULL);
  return fname;
}

char *archdep_default_autostart_disk_image_file_name(void) {
  const char *home;

  home = archdep_boot_path();
  return util_concat(home, "/autostart-", machine_get_name(), ".d64", NULL);
}

char *archdep_default_joymap_file_name(void) {
  return util_concat(archdep_boot_path(), "/joymap-", machine_get_name(),
                     ".vjm", NULL);
}

char *archdep_default_portable_resource_file_name(void) {
  return util_concat(archdep_boot_path(), "/vicerc", NULL);
}

char *archdep_default_resource_file_name(void) {
  return util_concat(archdep_user_config_path(), "/vicerc", NULL);
}

const char *archdep_home_path(void) { return archdep_boot_path(); }

void archdep_home_path_free(void) {}

char *archdep_get_vice_datadir(void) {
  return util_concat(archdep_boot_path(), NULL);
}

FILE *archdep_open_default_log_file(void) {
  // Don't ever need this for BMC64.
  return 0;
}

int archdep_default_logger(const char *level_string, const char *txt) {
  if (level_string == NULL) {
    level_string = "";
  }
  if (txt == NULL) {
    txt = "";
  }

  if (archdep_write_all(1, level_string, strlen(level_string)) < 0 ||
      archdep_write_all(1, txt, strlen(txt)) < 0 ||
      archdep_write_all(1, "\n", 1) < 0) {
    return -1;
  }
  return 0;
}

int archdep_default_logger_is_terminal(void) { return 1; }

int archdep_path_is_relative(const char *path) {
  if (path == NULL) {
    return 0;
  }

  return *path != '/';
}

int archdep_spawn(const char *name, char **argv, char **pstdout_redir,
                  const char *stderr_redir) {
  log_error(LOG_DEFAULT, "archdep_spawn unimpl: %s", name);
  return -1;
}

/* return malloced version of full pathname of orig_name */
int archdep_expand_path(char **return_path, const char *orig_name) {
  *return_path = lib_strdup(orig_name);
  return 0;
}

/** \brief  Sanitize \a name by removing invalid characters for the current OS
 *
 * \param[in,out]   name    0-terminated string
 */
void archdep_sanitize_filename(char *name) {
  while (*name != '\0') {
    int i = 0;
    while (illegal_name_tokens[i] != '\0') {
      if (illegal_name_tokens[i] == *name) {
        *name = '_';
        break;
      }
      i++;
    }
    name++;
  }
}

void archdep_startup_log_error(const char *format, ...) {
  char *tmp;
  va_list args;

  va_start(args, format);
  tmp = lib_mvsprintf(format, args);
  va_end(args);
  archdep_write_all(1, tmp, strlen(tmp));

  lib_free(tmp);
}

char *archdep_quote_parameter(const char *name) { return lib_strdup(name); }

char *archdep_quote_unzip(const char *name) { return lib_strdup(name); }

char *archdep_filename_parameter(const char *name) {
  char *exp;
  char *a;

  archdep_expand_path(&exp, name);
  a = archdep_quote_parameter(exp);
  lib_free(exp);
  return a;
}

char *archdep_tmpnam(void) { return lib_strdup(tmpnam(NULL)); }

FILE *archdep_mkstemp_fd(char **filename, const char *mode) {
  char *tmp;
  FILE *fd;

  tmp = lib_strdup(tmpnam(NULL));
  fd = fopen(tmp, mode);

  if (fd == NULL) {
    return NULL;
  }

  *filename = tmp;

  return fd;
}

int archdep_access(const char *pathname, int mode) {
  struct stat st;

  if (pathname == NULL) {
    return -1;
  }

  return stat(pathname, &st);
}

int archdep_close(int fd) { return close(fd); }

FILE *archdep_fdopen(int fd, const char *mode) { return NULL; }

int archdep_fseeko(FILE *stream, off_t offset, int whence) {
  return fseek(stream, (long)offset, whence);
}

off_t archdep_ftello(FILE *stream) { return (off_t)ftell(stream); }

int archdep_remove(const char *path) { return remove(path); }

int archdep_stat(const char *file_name, size_t *len,
                 unsigned int *isdir) {
  struct stat st;

  // The consequence of having to yield to let some fake kernel threads
  // run is troublesome.  When the emulated machine calls into us for
  // real file operations (i.e. iecdevice), we take a really long time
  // and effectively halt the yields we have in the video sync hooks. This
  // has a consequence of making the sound system think there's more data
  // in our buffers than there really is and when things go back to normal
  // it freaks out and thinks we've been running too slow.  That causes an
  // error and emulation stops.  To guard against this, yield here too.
  circle_yield();

  if (file_name == NULL) {
    goto fail;
  }

  // We can ignore these system files
  if (strcasecmp("./kernel7.img", file_name) == 0 ||
      strcasecmp("./kernel~1.img", file_name) == 0 ||
      strcasecmp("./kernel8-32.img", file_name) == 0 ||
      strcasecmp("./config.txt", file_name) == 0 ||
      strcasecmp("./cmdline.txt", file_name) == 0 ||
      strcasecmp("./bootstat.txt", file_name) == 0 ||
      strcasecmp("./bootcode.bin", file_name) == 0 ||
      strcasecmp("./start.elf", file_name) == 0) {
    goto fail;
  }

  if (stat(file_name, &st) == 0) {
    if (len != NULL) {
      *len = st.st_size;
    }
    if (isdir != NULL) {
      *isdir = S_ISDIR(st.st_mode);
    }
    return 0;
  }

fail:
  if (len != NULL) {
    *len = (size_t)-1;
  }
  if (isdir != NULL) {
    *isdir = 0;
  }
  return -1;
}

bool archdep_file_exists(const char *path) {
  struct stat st;

  if (path == NULL) {
    return false;
  }

  return stat(path, &st) == 0;
}

off_t archdep_file_size(FILE *stream) {
  long pos;
  long end;

  if (stream == NULL) {
    return -1;
  }

  pos = ftell(stream);
  if (pos < 0) {
    return -1;
  }
  if (fseek(stream, 0, SEEK_END) != 0) {
    return -1;
  }
  end = ftell(stream);
  if (fseek(stream, pos, SEEK_SET) != 0) {
    return -1;
  }

  return (off_t)end;
}

char *archdep_real_path(const char *pathname, char *resolved_pathname) {
  if (pathname == NULL || resolved_pathname == NULL) {
    return NULL;
  }

  strcpy(resolved_pathname, pathname);
  return resolved_pathname;
}

int archdep_real_path_equal(const char *path1, const char *path2) {
  if (path1 == NULL || path2 == NULL) {
    return 0;
  }

  return strcasecmp(path1, path2) == 0;
}

/* set permissions of given file to rw, respecting current umask */
int archdep_fix_permissions(const char *file_name) { return 0; }

int archdep_file_is_blockdev(const char *name) { return 0; }

int archdep_file_is_chardev(const char *name) { return 0; }

int archdep_rename(const char *oldpath, const char *newpath) {
  return rename(oldpath, newpath);
}

void archdep_shutdown(void) {
  if (default_path != NULL) {
    lib_free(default_path);
  }
}

const char *archdep_extra_title_text(void) { return NULL; }

bool archdep_is_exiting(void) { return false; }

int archdep_is_haiku(void) { return -1; }

void archdep_set_openmp_wait_policy(void) {}

void archdep_sound_enable_default_device_tracking(void) {}

char *archdep_get_vice_hotkeysdir(void) {
  return util_concat(archdep_boot_path(), "/hotkeys", NULL);
}

const char *archdep_user_config_path(void) { return archdep_boot_path(); }

void archdep_user_config_path_free(void) {}

void archdep_vice_exit(int excode) {}

int archdep_vice_atexit(void (*function)(void)) { return 0; }

int kbd_arch_get_host_mapping(void) { return KBD_MAPPING_US; }

int archdep_kbd_get_host_mapping(void) { return kbd_arch_get_host_mapping(); }

void archdep_program_name_free(void) {
  if (program_name != NULL) {
    lib_free(program_name);
    program_name = NULL;
  }
}

void archdep_program_path_set_argv0(char *argv0) {}

const char *archdep_program_path(void) { return archdep_boot_path(); }

void archdep_program_path_free(void) {}

void archdep_usleep(uint64_t usec) {
  unsigned long start = circle_get_ticks();

  while ((uint64_t)(circle_get_ticks() - start) < usec) {
    circle_yield();
  }
}

void archdep_sleep(unsigned int seconds) {
  archdep_usleep((uint64_t)seconds * 1000000U);
}

void tick_init(void) {}

tick_t tick_per_second(void) { return TICK_PER_SECOND; }

tick_t tick_now(void) { return (tick_t)circle_get_ticks(); }

tick_t tick_now_after(tick_t previous_tick) {
  tick_t now;

  do {
    now = tick_now();
  } while (now == previous_tick);

  return now;
}

tick_t tick_now_delta(tick_t previous_tick) {
  return tick_now() - previous_tick;
}

void tick_sleep(tick_t delay) { archdep_usleep(delay); }
