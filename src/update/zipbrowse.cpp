// zipbrowse.cpp — ZIP browsing/extraction glue over ZipReader.
//
// See zipbrowse.h. All state is static (single open archive); the file
// menus are modal, so no re-entrancy is possible.

#include "zipbrowse.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fatfs_update_storage.h"
#include "update/zip_reader.h"

extern "C" {
#include "ui.h"
}

// Reboot-persistent trace for hung extractions (the on-screen overlay dies
// with the hang; the log file survives it). Weak no-op here so this file
// stays portable: the Atari bridge overrides it with the synced debug log,
// other machines keep the no-op. C linkage on both ends.
extern "C" __attribute__((weak)) void zipbrowse_trace(const char *line) {
  (void)line;
}

namespace {

using bmx::update::FatFsZipSource;
using bmx::update::ZipCompression;
using bmx::update::ZipEntry;
using bmx::update::ZipExpectedFile;
using bmx::update::ZipExpectedInventory;
using bmx::update::ZipLimits;
using bmx::update::ZipReader;
using bmx::update::ZipStatus;
using bmx::update::ZipStatusString;
using bmx::update::ZipWorkspace;

ZipReader g_reader;
ZipWorkspace g_workspace;
ZipEntry g_entries[ZIPBROWSE_MAX_ENTRIES];
FatFsZipSource g_source;
bool g_open = false;
char g_open_path[256];
// Full-inventory manifests (ExtractOne demands exact file/dir counts).
ZipExpectedFile g_manifest_files[ZIPBROWSE_MAX_ENTRIES];
const char *g_manifest_dirs[ZIPBROWSE_MAX_ENTRIES];

struct FileSink : public bmx::update::ZipExtractSink {
  FILE *fp;
  bool ok;
  explicit FileSink() : fp(NULL), ok(false) {}
  bool BeginEntry(const ZipEntry &entry) {
    (void)entry;
    ok = false;
    return fp != NULL;
  }
  bool Write(bmx::update::ByteView bytes) {
    if (fp == NULL || bytes.data == NULL) return false;
    return fwrite(bytes.data, 1, bytes.size, fp) == bytes.size;
  }
  bool CommitEntry(const ZipEntry &entry) {
    (void)entry;
    ok = true;
    return true;
  }
  void AbortEntry(const ZipEntry &entry) {
    (void)entry;
    ok = false;
  }
};

int fail(char *err, size_t errsz, ZipStatus st) {  if (err != NULL && errsz > 0) {
    const char *detail = g_reader.FirstFailurePath();
    if (detail != NULL && detail[0] != '\0') {
      snprintf(err, errsz, "%s: %s", ZipStatusString(st), detail);
    } else {
      snprintf(err, errsz, "%s", ZipStatusString(st));
    }
  }
  return (int)st;
}

// Progress/cancel pumps for single-pass extraction. The overlay itself
// coalesces renders (5 Hz). Verify (produced = files checked) pumps every
// 1024 files with an indeterminate bar so huge manifests never go dark;
// stream (produced = bytes written) pumps every 256 KiB with a determinate
// bar. Returning false aborts with ZipStatus::Cancelled. Every pump point
// also appends a reboot-persistent trace line (zipbrowse_trace), so a hung
// device still tells us the exact phase and offset after a reboot.
struct ExtractUi {
  bool active;
  uint64_t next_pump;
};

// Open-phase pump: central scan reports produced=index/total=entries,
// local validation produced=entries+index/total=2*entries. Pumps every
// callback (the reader already throttles to 256-entry boundaries) with an
// indeterminate bar; false cancels the open.
bool ExtractOpenCallback(uint64_t produced, uint64_t total, void *ctx) {
  ExtractUi *ui = static_cast<ExtractUi *>(ctx);
  if ((produced & 255U) == 0U || produced == total) {
    char line[96];
    snprintf(line, sizeof(line), "open scan %llu/%llu",
             (unsigned long long)produced, (unsigned long long)total);
    zipbrowse_trace(line);
  }
  if (ui == NULL || !ui->active) {
    return true;
  }
  ui_update_progress_present(2 /* Zip */, 0, 0, 1, 0);
  return ui_update_progress_pump() == 0;
}

bool ExtractVerifyCallback(uint64_t produced, uint64_t total, void *ctx) {  ExtractUi *ui = static_cast<ExtractUi *>(ctx);
  if ((produced & 1023U) != 0U && produced != total) {
    return true;
  }
  {
    char line[96];
    snprintf(line, sizeof(line), "verify %llu/%llu",
             (unsigned long long)produced, (unsigned long long)total);
    zipbrowse_trace(line);
  }
  if (ui == NULL || !ui->active) {
    return true;
  }
  ui_update_progress_present(2 /* Zip */, 0, 0, 1, 0);
  return ui_update_progress_pump() == 0;
}

bool ExtractStreamCallback(uint64_t produced, uint64_t total, void *ctx) {
  ExtractUi *ui = static_cast<ExtractUi *>(ctx);
  if (ui == NULL || !ui->active) {
    return true;
  }
  if (produced < ui->next_pump && produced != total) {
    return true;
  }
  ui->next_pump = produced + 256U * 1024U;
  unsigned permille =
      total != 0U ? (unsigned)((produced * 1000U) / total) : 0U;
  if (permille > 1000U) {
    permille = 1000U;
  }
  {
    char line[96];
    snprintf(line, sizeof(line), "stream %llu/%llu",
             (unsigned long long)produced, (unsigned long long)total);
    zipbrowse_trace(line);
  }
  ui_update_progress_present(2 /* Zip */, permille, 1, 1, 0);
  return ui_update_progress_pump() == 0;
}

}  // namespace

int zipbrowse_open(const char *abs_path, char *err, size_t errsz) {
  zipbrowse_close();
  if (abs_path == NULL || abs_path[0] == '\0') {
    return fail(err, errsz, ZipStatus::InvalidArgument);
  }
  if (g_source.Open(abs_path) != bmx::update::FatFsStorageStatus::Ok) {
    if (err != NULL && errsz > 0) snprintf(err, errsz, "cannot open archive");
    return -1;
  }
  g_reader.SetWorkspace(&g_workspace);
  // User archives carry tool metadata (timestamps, Unix attrs, trailing
  // comments); skip it, and tolerate Windows separators. Ratio caps stay
  // for the updater (zip bombs); browsing streams single files, so a
  // runaway output fails visibly at the sink instead. Update packages
  // never enable any of this (strict by default).
  g_reader.SetLenientExtraFields(true);
  g_reader.SetLenientPaths(true);
  g_reader.SetLenientArchive(true);
  g_reader.SetLenientRatio(true);
  ZipLimits limits;
  limits.maximum_entries = ZIPBROWSE_MAX_ENTRIES;
  {
    char line[160];
    snprintf(line, sizeof(line), "open %s", abs_path);
    zipbrowse_trace(line);
  }
  ExtractUi ui;
  ui.active = (ui_update_progress_begin() != 0);
  ui.next_pump = 0;
  if (ui.active) {
    ui_update_progress_set_title("Extracting...");
    ui_update_progress_present(2 /* Zip */, 0, 0, 1, 0);
  }
  ZipStatus st = g_reader.Open(&g_source, g_entries, ZIPBROWSE_MAX_ENTRIES,
                               limits, ExtractOpenCallback, &ui);
  if (ui.active) {
    ui_update_progress_end();
  }
  {
    char line[96];
    snprintf(line, sizeof(line), "opened entries=%u status=%d",
             (unsigned)g_reader.inventory().entry_count, (int)st);
    zipbrowse_trace(line);
  }
  if (st != ZipStatus::Ok) {
    g_source.Close();
    return fail(err, errsz, st);
  }
  snprintf(g_open_path, sizeof(g_open_path), "%s", abs_path);
  g_open = true;
  return 0;
}

int zipbrowse_list(const char *prefix,
                   void (*visit)(const char *name, int is_dir, void *ctx),
                   void *ctx) {
  if (!g_open || visit == NULL) return -1;
  if (prefix == NULL) prefix = "";
  size_t plen = strlen(prefix);
  // Collect direct children, deduplicating directory names (static:
  // big packs can hold hundreds of dirs; must not live on the stack).
  static char dirs[2048][64];
  int ndirs = 0;
  int count = 0;
  const bmx::update::ZipInventory &inv = g_reader.inventory();
  // First pass: files and dir components.
  for (size_t i = 0; i < inv.entry_count; i++) {
    const ZipEntry &e = inv.entries[i];
    if (strncmp(e.path, prefix, plen) != 0) continue;
    const char *rest = e.path + plen;
    if (*rest == '\0') continue;  // the prefix dir itself
    const char *slash = strchr(rest, '/');
    if (slash != NULL) {
      // Immediate subdirectory.
      size_t len = (size_t)(slash - rest);
      if (len == 0 || len >= sizeof(dirs[0])) continue;
      bool seen = false;
      for (int d = 0; d < ndirs; d++) {
        if (strlen(dirs[d]) == len && strncmp(dirs[d], rest, len) == 0) {
          seen = true;
          break;
        }
      }
      if (!seen && ndirs < 2048) {
        memcpy(dirs[ndirs], rest, len);
        dirs[ndirs][len] = '\0';
        visit(dirs[ndirs], 1, ctx);
        ndirs++;
        count++;
      }
    } else {
      if (e.is_directory) continue;
      visit(rest, 0, ctx);
      count++;
      if (count >= ZIPBROWSE_MAX_ENTRIES) break;
    }
  }
  return count;
}

int zipbrowse_extract(const char *entry_path, const char *out_abs_path,
                      char *err, size_t errsz) {
  if (!g_open || entry_path == NULL || out_abs_path == NULL) {
    return fail(err, errsz, ZipStatus::InvalidArgument);
  }
  const bmx::update::ZipInventory &inv = g_reader.inventory();
  const ZipEntry *found = NULL;
  for (size_t i = 0; i < inv.entry_count; i++) {
    if (!inv.entries[i].is_directory &&
        strcmp(inv.entries[i].path, entry_path) == 0) {
      found = &inv.entries[i];
      break;
    }
  }
  if (found == NULL) {
    return fail(err, errsz, ZipStatus::EntryNotFound);
  }
  FileSink sink;
  sink.fp = fopen(out_abs_path, "wb");
  if (sink.fp == NULL) {
    if (err != NULL && errsz > 0) snprintf(err, errsz, "cannot write file");
    return -1;
  }
  // No SHA-256 available for ad-hoc archives; CRC + sizes are still
  // verified on the writing pass. The manifest must cover the whole
  // inventory exactly (VerifyExpectedInventory compares counts).
  size_t nf = 0, nd = 0;
  for (size_t i = 0; i < inv.entry_count; i++) {
    if (inv.entries[i].is_directory) {
      if (nd >= ZIPBROWSE_MAX_ENTRIES) break;
      g_manifest_dirs[nd++] = inv.entries[i].path;
    } else {
      if (nf >= ZIPBROWSE_MAX_ENTRIES) break;
      g_manifest_files[nf].path = inv.entries[i].path;
      g_manifest_files[nf].size = inv.entries[i].size;
      g_manifest_files[nf].compression = inv.entries[i].compression;
      g_manifest_files[nf].sha256 = NULL;
      nf++;
    }
  }
  ZipExpectedInventory expected;
  expected.files = g_manifest_files;
  expected.file_count = nf;
  expected.directories = g_manifest_dirs;
  expected.directory_count = nd;
  // Single pass: inflate straight into the output while CRC/size verify
  // before commit. On any failure (corrupt or cancelled) the partial file
  // is removed below, so nothing uncommitted ever survives.
  ExtractUi ui;
  ui.active = (ui_update_progress_begin() != 0);
  ui.next_pump = 0;
  if (ui.active) {
    ui_update_progress_set_title("Extracting...");
    ui_update_progress_present(2 /* Zip */, 0, 1, 1, 0);
  }
  {
    char line[160];
    snprintf(line, sizeof(line), "extract %s size=%llu files=%u", found->path,
             (unsigned long long)found->size, (unsigned)nf);
    zipbrowse_trace(line);
  }
  ZipStatus st = g_reader.ExtractOneSinglePass(
      found->path, expected, &sink, ExtractVerifyCallback, &ui,
      ExtractStreamCallback, &ui);
  if (ui.active) {
    ui_update_progress_end();
  }
  {
    char line[96];
    snprintf(line, sizeof(line), "done status=%d", (int)st);
    zipbrowse_trace(line);
  }
  fclose(sink.fp);
  sink.fp = NULL;
  if (st != ZipStatus::Ok || !sink.ok) {
    remove(out_abs_path);
    return fail(err, errsz, st);
  }
  return 0;
}

void zipbrowse_close(void) {
  if (g_open) {
    g_source.Close();
    g_open = false;
    g_open_path[0] = '\0';
  }
}

int zipbrowse_mkdir_chain(const char *abs_path) {
  if (abs_path == NULL || abs_path[0] == '\0') return -1;
  char tmp[256];
  snprintf(tmp, sizeof(tmp), "%s", abs_path);
  size_t len = strlen(tmp);
  for (size_t i = 1; i < len; i++) {
    if (tmp[i] == '/') {
      tmp[i] = '\0';
      mkdir(tmp, 0755);  // best effort; EEXIST is fine
      tmp[i] = '/';
    }
  }
  return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static int wipe_rec(const char *dir, int *removed) {
  DIR *dp = opendir(dir);
  if (dp == NULL) return 0;  // missing dir is fine
  struct dirent *ep;
  char child[300];
  while ((ep = readdir(dp)) != NULL) {
    if (strcmp(ep->d_name, ".") == 0 || strcmp(ep->d_name, "..") == 0) {
      continue;
    }
    snprintf(child, sizeof(child), "%s/%s", dir, ep->d_name);
    // Try file first, then recurse as directory.
    if (remove(child) == 0) {
      (*removed)++;
    } else {
      wipe_rec(child, removed);
      rmdir(child);
    }
  }
  closedir(dp);
  return 0;
}

int zipbrowse_wipe_dir(const char *abs_dir) {
  if (abs_dir == NULL) return -1;
  int removed = 0;
  wipe_rec(abs_dir, &removed);
  return removed;
}
