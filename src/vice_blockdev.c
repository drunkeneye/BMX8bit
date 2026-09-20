/*
 * Minimal block device backend for the bare-metal VICE build.
 * This keeps raw disk image support working without SDL/GTK arch code.
 */

#include "vice.h"

#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#include "blockdev.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

static int g_blockdev_fd = -1;

static int blockdev_seek(unsigned int track, unsigned int sector) {
  off_t offset = (off_t) (((track - 1) * 40u + sector) * 256u);
  return lseek(g_blockdev_fd, offset, SEEK_SET) >= 0 ? 0 : -1;
}

int blockdev_open(const char *name, unsigned int *read_only) {
  int flags = (*read_only ? O_RDONLY : O_RDWR) | O_BINARY;
  g_blockdev_fd = open(name, flags, 0);

  if (g_blockdev_fd < 0 && !*read_only) {
    g_blockdev_fd = open(name, O_RDONLY | O_BINARY, 0);
    if (g_blockdev_fd >= 0) {
      *read_only = 1;
    }
  }

  return g_blockdev_fd >= 0 ? 0 : -1;
}

int blockdev_close(void) {
  int rc = 0;

  if (g_blockdev_fd >= 0) {
    rc = close(g_blockdev_fd);
    g_blockdev_fd = -1;
  }

  return rc;
}

int blockdev_read_sector(uint8_t *buf, unsigned int track, unsigned int sector) {
  if (g_blockdev_fd < 0 || blockdev_seek(track, sector) < 0) {
    return -1;
  }

  return read(g_blockdev_fd, buf, 256) == 256 ? 0 : -1;
}

int blockdev_write_sector(const uint8_t *buf, unsigned int track, unsigned int sector) {
  if (g_blockdev_fd < 0 || blockdev_seek(track, sector) < 0) {
    return -1;
  }

  return write(g_blockdev_fd, buf, 256) == 256 ? 0 : -1;
}

void blockdev_init(void) {}

int blockdev_resources_init(void) {
  return 0;
}

int blockdev_cmdline_options_init(void) {
  return 0;
}
