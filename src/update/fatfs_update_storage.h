#ifndef BMX_UPDATE_FATFS_UPDATE_STORAGE_H
#define BMX_UPDATE_FATFS_UPDATE_STORAGE_H

#include "http_response_parser.h"
#include "sha256.h"
#include "zip_reader.h"

#if defined(RASPI_COMPILE) || defined(BMX_UPDATE_USE_FATFS)
#include <ff.h>
#define BMX_UPDATE_HAS_FATFS 1
#else
#define BMX_UPDATE_HAS_FATFS 0
#endif

namespace bmx {
namespace update {

// Includes the volume prefix, separators and terminating NUL.  ZIP member
// names are capped separately at kZipMaximumPathBytes.
static const size_t kFatFsUpdatePathBytes = 512U;

enum class FatFsStorageStatus : uint8_t {
    Ok = 0,
    BackendUnavailable,
    InvalidArgument,
    InvalidPath,
    PathTooLong,
    NotOpen,
    Busy,
    NotFound,
    AlreadyExists,
    NotDirectory,
    IsDirectory,
    OpenFailed,
    ReadFailed,
    WriteFailed,
    SeekFailed,
    SyncFailed,
    CloseFailed,
    StatFailed,
    MkdirFailed,
    RemoveFailed,
    RenameFailed,
    SizeMismatch,
    LimitExceeded,
    HashFailed,
    HashMismatch
};

struct FatFsPathInfo {
    bool exists;
    bool is_directory;
    uint64_t size;
};

// Absolute update paths use an explicit FatFs volume (for example SYS:/) and
// relative paths use forward slashes only.  Both reject dot components,
// backslashes, controls and non-ASCII bytes.
FatFsStorageStatus ValidateFatFsAbsolutePath(const char *path);

FatFsStorageStatus FatFsStatPath(const char *path, FatFsPathInfo *info);
FatFsStorageStatus FatFsRemoveFileIfPresent(const char *path);

class FatFsDownloadSink : public HttpBodySink {
public:
    FatFsDownloadSink();
    ~FatFsDownloadSink();

    // part_path must end in .part.  Starting a new explicit download removes
    // only that stale .part file; it never replaces an existing final file.
    FatFsStorageStatus Start(
        const char *part_path, const char *final_path, uint64_t expected_size,
        const uint8_t expected_sha256[kSha256DigestBytes]);
    bool Write(const uint8_t *data, size_t size);

    // Authenticates bytes while they arrive, syncs them, and performs the
    // no-replace rename without extra full reads.
    // ZIP parsing and signed per-file hashes validate the stored archive
    // before any member is installed.
    FatFsStorageStatus FinishStreamingVerified();
    void Abort(bool remove_part);

private:
    FatFsDownloadSink(const FatFsDownloadSink &);
    FatFsDownloadSink &operator=(const FatFsDownloadSink &);

#if BMX_UPDATE_HAS_FATFS
    FIL file_;
#endif
    bool open_;
    uint64_t expected_size_;
    uint64_t bytes_written_;
    uint8_t expected_sha256_[kSha256DigestBytes];
    Sha256 streaming_sha256_;
    char part_path_[kFatFsUpdatePathBytes];
    char final_path_[kFatFsUpdatePathBytes];
};

class FatFsZipSource : public SeekableZipSource {
public:
    FatFsZipSource();
    ~FatFsZipSource();

    FatFsStorageStatus Open(const char *path);
    FatFsStorageStatus Close();
    bool GetSize(uint64_t *size);
    bool ReadAt(uint64_t offset, uint8_t *destination, size_t size);

private:
    FatFsZipSource(const FatFsZipSource &);
    FatFsZipSource &operator=(const FatFsZipSource &);

#if BMX_UPDATE_HAS_FATFS
    FIL file_;
#endif
    bool open_;
    uint64_t size_;
    // Read cursor cache: StreamFile reads strictly sequentially, and every
    // f_lseek on FatFS without a link map walks the cluster chain from the
    // last known position (O(n) per seek, O(n^2) per file). Skipping the
    // seek when already positioned turns extraction into one pass.
    uint64_t position_;
    bool positioned_;
};

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_FATFS_UPDATE_STORAGE_H
