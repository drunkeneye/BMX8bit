// zipbrowse.h — browse/extract arbitrary ZIP archives for the file menus.
//
// Unlike the update flow (signed manifests), this opens any archive and
// extracts single entries chosen by the user. ZipReader still validates
// structure, paths (ASCII, no traversal), CRC and sizes; hostile archives
// fail with a human-readable status string.
//
// C API (menu.c is C); implementation in zipbrowse.cpp.

#ifndef BMX_UPDATE_ZIPBROWSE_H
#define BMX_UPDATE_ZIPBROWSE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum entries listed from one archive (ZipReader inventory bound).
// Sized for complete game packs (30k+ files): ~11MB static, paid once.
#define ZIPBROWSE_MAX_ENTRIES 32768

// Open + validate an archive (absolute volume path, e.g. "USER:/x/y.zip").
// Returns 0 on success; else a ZipStatus code with text in err.
int zipbrowse_open(const char *abs_path, char *err, size_t errsz);

// List direct children of prefix ("" for root, else "a/b/"). Calls visit()
// once per child (name = single component, is_dir flag). Returns child
// count or -1 on misuse (no open archive).
int zipbrowse_list(const char *prefix, void (*visit)(const char *name,
                                                     int is_dir, void *ctx),
                   void *ctx);

// Extract one entry (full internal path) to an absolute output path.
// Parent directories of out_abs_path must exist (see zipbrowse_mkdir_chain).
// Returns 0 on success (CRC/size verified), else ZipStatus with text in err.
int zipbrowse_extract(const char *entry_path, const char *out_abs_path,
                      char *err, size_t errsz);

void zipbrowse_close(void);

// mkdir -p for absolute volume paths. Returns 0 on success.
int zipbrowse_mkdir_chain(const char *abs_path);

// Remove every file under abs_dir (recursively). Returns removed count,
// or -1 if the dir cannot be opened (missing dir is fine, returns 0).
int zipbrowse_wipe_dir(const char *abs_dir);

#ifdef __cplusplus
}
#endif

#endif  // BMX_UPDATE_ZIPBROWSE_H
