#include "update/fatfs_config_snapshot.h"

#include <ff.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace bmx {
namespace update {

namespace {

bool ValidVolume(const char *volume) {
    if (volume == 0) return false;
    const size_t length = strlen(volume);
    if (length < 2U || length > 12U || volume[length - 1U] != ':') return false;
    for (size_t i = 0U; i + 1U < length; ++i) {
        const unsigned char value = static_cast<unsigned char>(volume[i]);
        if (!isalnum(value) && value != '_' && value != '-') return false;
    }
    return true;
}

bool ValidSimpleName(const char *name) {
    if (name == 0) return false;
    const size_t length = strlen(name);
    if (length == 0U || length > kMaximumConfigPathBytes) return false;
    for (size_t i = 0U; i < length; ++i) {
        const unsigned char value = static_cast<unsigned char>(name[i]);
        if (value < 0x20U || value > 0x7eU || value == '/' || value == '\\' ||
            value == ':') {
            return false;
        }
    }
    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

bool ValidRelativePath(const char *path) {
    if (path == 0) return false;
    const size_t length = strlen(path);
    if (length == 0U || length > kMaximumConfigPathBytes || path[0] == '/' ||
        path[length - 1U] == '/') return false;
    size_t component = 0U;
    for (size_t i = 0U; i <= length; ++i) {
        if (i != length && path[i] != '/') {
            const unsigned char value = static_cast<unsigned char>(path[i]);
            if (value < 0x20U || value > 0x7eU || value == '\\' || value == ':') {
                return false;
            }
            continue;
        }
        const size_t size = i - component;
        if (size == 0U || (size == 1U && path[component] == '.') ||
            (size == 2U && path[component] == '.' &&
             path[component + 1U] == '.')) return false;
        component = i + 1U;
    }
    return true;
}

bool IsSettingsName(const char *name) {
    if (!ValidSimpleName(name)) return false;
    const size_t length = strlen(name);
    static const char prefix[] = "settings";
    static const char suffix[] = ".txt";
    if (length < sizeof(prefix) - 1U + sizeof(suffix) - 1U ||
        memcmp(name, prefix, sizeof(prefix) - 1U) != 0 ||
        memcmp(name + length - (sizeof(suffix) - 1U), suffix,
               sizeof(suffix) - 1U) != 0) {
        return false;
    }
    for (size_t i = sizeof(prefix) - 1U;
         i < length - (sizeof(suffix) - 1U); ++i) {
        const char value = name[i];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '-' || value == '_')) {
            return false;
        }
    }
    return true;
}

size_t AreaIndex(ConfigArea area) {
    return static_cast<size_t>(area) - 1U;
}

}  // namespace

FatFsConfigSnapshot::FatFsConfigSnapshot()
    : owned_count_(0U), snapshot_() {
    memset(owned_, 0, sizeof(owned_));
    memset(views_, 0, sizeof(views_));
    memset(view_counts_, 0, sizeof(view_counts_));
    memset(areas_, 0, sizeof(areas_));
}

FatFsConfigSnapshot::~FatFsConfigSnapshot() { Reset(); }

void FatFsConfigSnapshot::Reset() {
    for (size_t i = 0U; i < owned_count_; ++i) {
        delete[] owned_[i].bytes;
        owned_[i].bytes = 0;
        owned_[i].size = 0U;
        owned_[i].path[0] = '\0';
    }
    owned_count_ = 0U;
    memset(views_, 0, sizeof(views_));
    memset(view_counts_, 0, sizeof(view_counts_));
    memset(areas_, 0, sizeof(areas_));
    snapshot_ = ConfigSnapshot();
}

FatFsConfigSnapshotStatus FatFsConfigSnapshot::ReadOptional(
    const char *volume, const char *relative_path, size_t *owned_index,
    bool *present) {
    if (!ValidVolume(volume) || !ValidRelativePath(relative_path) ||
        owned_index == 0 || present == 0) {
        return FatFsConfigSnapshotStatus::InvalidArgument;
    }
    *present = false;
    *owned_index = 0U;
    char absolute[kMaximumConfigPathBytes + 32U];
    const int written = snprintf(absolute, sizeof(absolute), "%s/%s", volume,
                                 relative_path);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(absolute)) {
        return FatFsConfigSnapshotStatus::PathInvalid;
    }
    FILINFO info;
    const FRESULT stat = f_stat(absolute, &info);
    if (stat == FR_NO_FILE || stat == FR_NO_PATH) {
        return FatFsConfigSnapshotStatus::Ok;
    }
    if (stat != FR_OK) return FatFsConfigSnapshotStatus::StatFailed;
    if ((info.fattrib & AM_DIR) != 0U) {
        return FatFsConfigSnapshotStatus::PathInvalid;
    }
    if (info.fsize > kMaximumConfigFileBytes || info.fsize > SIZE_MAX) {
        return FatFsConfigSnapshotStatus::FileTooLarge;
    }
    if (owned_count_ >= kMaximumOwnedFiles) {
        return FatFsConfigSnapshotStatus::TooManyFiles;
    }
    uint8_t *bytes = new uint8_t[info.fsize == 0U ? 1U
                                                   : static_cast<size_t>(info.fsize)];
    if (bytes == 0) return FatFsConfigSnapshotStatus::AllocationFailed;
    FIL file;
    FRESULT result = f_open(&file, absolute, FA_READ);
    if (result != FR_OK) {
        delete[] bytes;
        return FatFsConfigSnapshotStatus::OpenFailed;
    }
    size_t offset = 0U;
    while (offset < static_cast<size_t>(info.fsize)) {
        const size_t remaining = static_cast<size_t>(info.fsize) - offset;
        const UINT request = remaining > 16384U
                                 ? 16384U : static_cast<UINT>(remaining);
        UINT amount = 0U;
        result = f_read(&file, bytes + offset, request, &amount);
        if (result != FR_OK || amount != request) {
            f_close(&file);
            delete[] bytes;
            return FatFsConfigSnapshotStatus::ReadFailed;
        }
        offset += amount;
    }
    if (f_size(&file) != info.fsize) {
        f_close(&file);
        delete[] bytes;
        return FatFsConfigSnapshotStatus::ReadFailed;
    }
    if (f_close(&file) != FR_OK) {
        delete[] bytes;
        return FatFsConfigSnapshotStatus::CloseFailed;
    }
    FILINFO after;
    if (f_stat(absolute, &after) != FR_OK ||
        (after.fattrib & AM_DIR) != 0U || after.fsize != info.fsize) {
        delete[] bytes;
        return FatFsConfigSnapshotStatus::ReadFailed;
    }
    OwnedFile &owned = owned_[owned_count_];
    memcpy(owned.path, relative_path, strlen(relative_path) + 1U);
    owned.bytes = bytes;
    owned.size = offset;
    *owned_index = owned_count_++;
    *present = true;
    return FatFsConfigSnapshotStatus::Ok;
}

FatFsConfigSnapshotStatus FatFsConfigSnapshot::AddView(ConfigArea area,
                                                        size_t owned_index) {
    if (!IsKnownConfigArea(area) || owned_index >= owned_count_) {
        return FatFsConfigSnapshotStatus::InvalidArgument;
    }
    const size_t index = AreaIndex(area);
    if (view_counts_[index] >= kMaximumConfigFilesPerArea) {
        return FatFsConfigSnapshotStatus::TooManyFiles;
    }
    size_t aggregate = owned_[owned_index].size;
    for (size_t i = 0U; i < view_counts_[index]; ++i) {
        if (aggregate > kMaximumConfigAreaBytes - views_[index][i].content.size) {
            return FatFsConfigSnapshotStatus::AreaTooLarge;
        }
        aggregate += views_[index][i].content.size;
    }
    ConfigFileView &view = views_[index][view_counts_[index]++];
    view.path = owned_[owned_index].path;
    view.content = ByteView(owned_[owned_index].bytes, owned_[owned_index].size);
    return FatFsConfigSnapshotStatus::Ok;
}

FatFsConfigSnapshotStatus FatFsConfigSnapshot::Load(const char *volume) {
    Reset();
    if (!ValidVolume(volume)) return FatFsConfigSnapshotStatus::InvalidArgument;

    struct FixedFile { ConfigArea area; const char *path; };
    static const FixedFile fixed[] = {
        {ConfigArea::Machines, "machines.ini"},
        {ConfigArea::Machines, "machines.defaults.ini"},
        {ConfigArea::Machines, "machines.local.ini"},
        {ConfigArea::ConfigManagedBlock, "config.txt"},
        {ConfigArea::UpdateState, "BMX-BUILD.json"}
    };
    for (size_t i = 0U; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
        size_t owned = 0U;
        bool present = false;
        FatFsConfigSnapshotStatus status =
            ReadOptional(volume, fixed[i].path, &owned, &present);
        if (status != FatFsConfigSnapshotStatus::Ok) {
            Reset();
            return status;
        }
        if (present &&
            (status = AddView(fixed[i].area, owned)) !=
                FatFsConfigSnapshotStatus::Ok) {
            Reset();
            return status;
        }
    }

    size_t cmdline = 0U;
    bool cmdline_present = false;
    FatFsConfigSnapshotStatus status =
        ReadOptional(volume, "cmdline.txt", &cmdline, &cmdline_present);
    if (status != FatFsConfigSnapshotStatus::Ok) {
        Reset();
        return status;
    }
    if (cmdline_present) {
        status = AddView(ConfigArea::CmdlineManagedKeys, cmdline);
        if (status == FatFsConfigSnapshotStatus::Ok) {
            status = AddView(ConfigArea::Network, cmdline);
        }
        if (status != FatFsConfigSnapshotStatus::Ok) {
            Reset();
            return status;
        }
    }

    char root[16];
    const int root_written = snprintf(root, sizeof(root), "%s/", volume);
    if (root_written <= 0 || static_cast<size_t>(root_written) >= sizeof(root)) {
        Reset();
        return FatFsConfigSnapshotStatus::PathInvalid;
    }
    DIR directory;
    if (f_opendir(&directory, root) != FR_OK) {
        Reset();
        return FatFsConfigSnapshotStatus::DirectoryReadFailed;
    }
    char settings[kMaximumConfigFilesPerArea][kMaximumConfigPathBytes + 1U] = {};
    size_t settings_count = 0U;
    while (true) {
        FILINFO info;
        const FRESULT read = f_readdir(&directory, &info);
        if (read != FR_OK) {
            f_closedir(&directory);
            Reset();
            return FatFsConfigSnapshotStatus::DirectoryReadFailed;
        }
        if (info.fname[0] == '\0') break;
        if ((info.fattrib & AM_DIR) != 0U || !IsSettingsName(info.fname)) continue;
        if (settings_count >= kMaximumConfigFilesPerArea) {
            f_closedir(&directory);
            Reset();
            return FatFsConfigSnapshotStatus::TooManyFiles;
        }
        memcpy(settings[settings_count], info.fname, strlen(info.fname) + 1U);
        ++settings_count;
    }
    if (f_closedir(&directory) != FR_OK) {
        Reset();
        return FatFsConfigSnapshotStatus::DirectoryReadFailed;
    }
    for (size_t i = 1U; i < settings_count; ++i) {
        char value[kMaximumConfigPathBytes + 1U];
        memcpy(value, settings[i], sizeof(value));
        size_t position = i;
        while (position > 0U && strcmp(settings[position - 1U], value) > 0) {
            memcpy(settings[position], settings[position - 1U],
                   sizeof(settings[position]));
            --position;
        }
        memcpy(settings[position], value, sizeof(settings[position]));
    }
    for (size_t i = 0U; i < settings_count; ++i) {
        size_t owned = 0U;
        bool present = false;
        status = ReadOptional(volume, settings[i], &owned, &present);
        if (status != FatFsConfigSnapshotStatus::Ok || !present ||
            (status = AddView(ConfigArea::Settings, owned)) !=
                FatFsConfigSnapshotStatus::Ok) {
            Reset();
            return status == FatFsConfigSnapshotStatus::Ok
                       ? FatFsConfigSnapshotStatus::StatFailed : status;
        }
    }

    for (size_t i = 0U; i < kConfigMigrationAreaCount; ++i) {
        areas_[i].area = static_cast<ConfigArea>(i + 1U);
        areas_[i].files = views_[i];
        areas_[i].file_count = view_counts_[i];
    }
    snapshot_.areas = areas_;
    snapshot_.area_count = kConfigMigrationAreaCount;
    return FatFsConfigSnapshotStatus::Ok;
}

const char *FatFsConfigSnapshotStatusString(FatFsConfigSnapshotStatus status) {
    switch (status) {
    case FatFsConfigSnapshotStatus::Ok: return "ok";
    case FatFsConfigSnapshotStatus::InvalidArgument: return "invalid argument";
    case FatFsConfigSnapshotStatus::TooManyFiles: return "too many config files";
    case FatFsConfigSnapshotStatus::PathInvalid: return "invalid config path";
    case FatFsConfigSnapshotStatus::FileTooLarge: return "config file too large";
    case FatFsConfigSnapshotStatus::AreaTooLarge: return "config area too large";
    case FatFsConfigSnapshotStatus::AllocationFailed: return "not enough memory";
    case FatFsConfigSnapshotStatus::DirectoryReadFailed: return "directory read failed";
    case FatFsConfigSnapshotStatus::StatFailed: return "config stat failed";
    case FatFsConfigSnapshotStatus::OpenFailed: return "config open failed";
    case FatFsConfigSnapshotStatus::ReadFailed: return "config read failed";
    case FatFsConfigSnapshotStatus::CloseFailed: return "config close failed";
    }
    return "unknown config snapshot error";
}

}  // namespace update
}  // namespace bmx
