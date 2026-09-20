#ifndef BMX_UPDATE_FATFS_CONFIG_SNAPSHOT_H
#define BMX_UPDATE_FATFS_CONFIG_SNAPSHOT_H

#include "update/config_migration.h"

namespace bmx {
namespace update {

enum class FatFsConfigSnapshotStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    TooManyFiles,
    PathInvalid,
    FileTooLarge,
    AreaTooLarge,
    AllocationFailed,
    DirectoryReadFailed,
    StatFailed,
    OpenFailed,
    ReadFailed,
    CloseFailed
};

// Owns a stable, bounded copy of every configuration byte used for update
// classification.  The caller may retain snapshot() until Load() is called
// again or this object is destroyed.  Nothing here writes to the card.
class FatFsConfigSnapshot {
 public:
    FatFsConfigSnapshot();
    ~FatFsConfigSnapshot();

    FatFsConfigSnapshotStatus Load(const char *volume);
    const ConfigSnapshot &snapshot() const { return snapshot_; }
    void Reset();

 private:
    FatFsConfigSnapshot(const FatFsConfigSnapshot &);
    FatFsConfigSnapshot &operator=(const FatFsConfigSnapshot &);

    static const size_t kMaximumOwnedFiles = 24U;

    struct OwnedFile {
        char path[kMaximumConfigPathBytes + 1U];
        uint8_t *bytes;
        size_t size;
    };

    FatFsConfigSnapshotStatus ReadOptional(const char *volume,
                                           const char *relative_path,
                                           size_t *owned_index,
                                           bool *present);
    FatFsConfigSnapshotStatus AddView(ConfigArea area, size_t owned_index);

    OwnedFile owned_[kMaximumOwnedFiles];
    size_t owned_count_;
    ConfigFileView views_[kConfigMigrationAreaCount]
                         [kMaximumConfigFilesPerArea];
    size_t view_counts_[kConfigMigrationAreaCount];
    ConfigAreaSnapshot areas_[kConfigMigrationAreaCount];
    ConfigSnapshot snapshot_;
};

const char *FatFsConfigSnapshotStatusString(FatFsConfigSnapshotStatus status);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_FATFS_CONFIG_SNAPSHOT_H
