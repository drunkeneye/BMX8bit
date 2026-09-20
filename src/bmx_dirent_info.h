#ifndef BMX_DIRENT_INFO_H
#define BMX_DIRENT_INFO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FatFs metadata returned by the same sequential scan as readdir(). */
typedef struct bmx_dirent_info_s {
    uint64_t size;
    unsigned int fat_attributes;
} bmx_dirent_info_t;

#define BMX_DIRENT_FAT_ATTR_READ_ONLY 0x01U
#define BMX_DIRENT_FAT_ATTR_DIRECTORY 0x10U

/*
 * Retrieve the metadata belonging to the most recent entry returned for
 * directory.  entry must be that exact readdir()/readdir_r() result.
 * Returns 1 when metadata is available and 0 otherwise.
 *
 * void pointers keep this small bridge independent of Circle's nonstandard
 * DIR and dirent header locations.
 */
int bmx_readdir_get_info(void *directory, const void *entry,
                         bmx_dirent_info_t *info);

#ifdef __cplusplus
}
#endif

#endif
