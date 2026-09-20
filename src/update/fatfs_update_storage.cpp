#include "fatfs_update_storage.h"

#include "fat_path_policy.h"

#include <string.h>

namespace bmx {
namespace update {

namespace {

static_assert(kZipMaximumPathBytes == kFatReleasePathMaximumBytes,
              "storage and ZIP release path limits must remain identical");

unsigned char AsciiLower(unsigned char value)
{
    if (value >= 'A' && value <= 'Z') {
        return static_cast<unsigned char>(value + ('a' - 'A'));
    }
    return value;
}

bool BoundedStringLength(const char *text, size_t maximum_with_nul,
                         size_t *length)
{
    if (text == 0 || length == 0 || maximum_with_nul == 0U) return false;
    size_t index = 0U;
    while (index < maximum_with_nul && text[index] != 0) ++index;
    if (index == maximum_with_nul) return false;
    *length = index;
    return true;
}

FatFsStorageStatus MapPathStatus(FatPathValidationStatus status)
{
    switch (status) {
    case FatPathValidationStatus::Ok:
        return FatFsStorageStatus::Ok;
    case FatPathValidationStatus::InvalidArgument:
        return FatFsStorageStatus::InvalidArgument;
    case FatPathValidationStatus::PathTooLong:
        return FatFsStorageStatus::PathTooLong;
    case FatPathValidationStatus::InvalidPath:
        return FatFsStorageStatus::InvalidPath;
    }
    return FatFsStorageStatus::InvalidPath;
}

bool CopyPath(const char *source, char *destination, size_t capacity)
{
    if (source == 0 || destination == 0 || capacity == 0U) return false;
    const size_t size = strlen(source);
    if (size >= capacity) return false;
    memcpy(destination, source, size + 1U);
    return true;
}

bool EndsWith(const char *text, const char *suffix)
{
    if (text == 0 || suffix == 0) return false;
    const size_t text_size = strlen(text);
    const size_t suffix_size = strlen(suffix);
    return text_size >= suffix_size &&
           memcmp(text + text_size - suffix_size, suffix, suffix_size) == 0;
}

bool SameVolume(const char *left, const char *right)
{
    size_t index = 0U;
    while (left[index] != ':' && right[index] != ':') {
        if (left[index] == 0 || right[index] == 0 ||
            AsciiLower(static_cast<unsigned char>(left[index])) !=
                AsciiLower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
        ++index;
    }
    return left[index] == ':' && right[index] == ':';
}

#if BMX_UPDATE_HAS_FATFS

static const size_t kMaximumFatFsIoBytes =
    static_cast<size_t>(static_cast<UINT>(-1));

bool IsMissing(FRESULT result)
{
    return result == FR_NO_FILE || result == FR_NO_PATH;
}

FatFsStorageStatus CloseFile(FIL *file)
{
    return f_close(file) == FR_OK ? FatFsStorageStatus::Ok
                                  : FatFsStorageStatus::CloseFailed;
}

FatFsStorageStatus StatValidated(const char *path, FatFsPathInfo *info)
{
    FILINFO file_info;
    memset(&file_info, 0, sizeof(file_info));
    const FRESULT result = f_stat(path, &file_info);
    if (IsMissing(result)) {
        info->exists = false;
        info->is_directory = false;
        info->size = 0U;
        return FatFsStorageStatus::Ok;
    }
    if (result != FR_OK) return FatFsStorageStatus::StatFailed;
    info->exists = true;
    info->is_directory = (file_info.fattrib & AM_DIR) != 0U;
    info->size = info->is_directory ? 0U
                                    : static_cast<uint64_t>(file_info.fsize);
    return FatFsStorageStatus::Ok;
}

FatFsStorageStatus EnsureOneDirectory(const char *path)
{
    const FRESULT result = f_mkdir(path);
    if (result != FR_OK && result != FR_EXIST) {
        return FatFsStorageStatus::MkdirFailed;
    }
    FatFsPathInfo info;
    const FatFsStorageStatus status = StatValidated(path, &info);
    if (status != FatFsStorageStatus::Ok) return status;
    if (!info.exists) return FatFsStorageStatus::MkdirFailed;
    if (!info.is_directory) return FatFsStorageStatus::NotDirectory;
    return FatFsStorageStatus::Ok;
}

FatFsStorageStatus EnsureParentDirectories(const char *path)
{
    char working[kFatFsUpdatePathBytes];
    if (!CopyPath(path, working, sizeof(working))) {
        return FatFsStorageStatus::PathTooLong;
    }
    char *volume = strchr(working, ':');
    if (volume == 0 || volume[1] != '/') {
        return FatFsStorageStatus::InvalidPath;
    }
    for (char *cursor = volume + 2; *cursor != 0; ++cursor) {
        if (*cursor != '/') continue;
        *cursor = 0;
        const FatFsStorageStatus status = EnsureOneDirectory(working);
        *cursor = '/';
        if (status != FatFsStorageStatus::Ok) return status;
    }
    return FatFsStorageStatus::Ok;
}

FatFsStorageStatus WriteAll(FIL *file, const uint8_t *data, size_t size)
{
    while (size != 0U) {
        const size_t limited = size > kMaximumFatFsIoBytes
                                   ? kMaximumFatFsIoBytes
                                   : size;
        UINT written = 0U;
        const FRESULT result =
            f_write(file, data, static_cast<UINT>(limited), &written);
        if (result != FR_OK || written != limited) {
            return FatFsStorageStatus::WriteFailed;
        }
        data += limited;
        size -= limited;
    }
    return FatFsStorageStatus::Ok;
}

FatFsStorageStatus ReadExact(FIL *file, uint8_t *data, size_t size)
{
    while (size != 0U) {
        const size_t limited = size > kMaximumFatFsIoBytes
                                   ? kMaximumFatFsIoBytes
                                   : size;
        UINT received = 0U;
        const FRESULT result =
            f_read(file, data, static_cast<UINT>(limited), &received);
        if (result != FR_OK || received != limited) {
            return FatFsStorageStatus::ReadFailed;
        }
        data += limited;
        size -= limited;
    }
    return FatFsStorageStatus::Ok;
}

#endif  // BMX_UPDATE_HAS_FATFS

}  // namespace

FatFsStorageStatus ValidateFatFsAbsolutePath(const char *path)
{
    if (path == 0 || path[0] == 0) return FatFsStorageStatus::InvalidArgument;
    size_t length = 0U;
    if (!BoundedStringLength(path, kFatFsUpdatePathBytes, &length)) {
        return FatFsStorageStatus::PathTooLong;
    }
    size_t colon = 0U;
    while (colon < length && path[colon] != ':') {
        const unsigned char value = static_cast<unsigned char>(path[colon]);
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '_')) {
            return FatFsStorageStatus::InvalidPath;
        }
        ++colon;
    }
    if (colon == 0U || colon > 15U || colon + 1U >= length ||
        path[colon] != ':' || path[colon + 1U] != '/') {
        return FatFsStorageStatus::InvalidPath;
    }
    if (colon + 2U == length) return FatFsStorageStatus::Ok;
    return MapPathStatus(ValidateFatRelativePathBytes(
        reinterpret_cast<const uint8_t *>(path + colon + 2U),
        length - colon - 2U, kFatFsUpdatePathBytes - 1U));
}

FatFsStorageStatus FatFsStatPath(const char *path, FatFsPathInfo *info)
{
    if (info == 0) return FatFsStorageStatus::InvalidArgument;
    info->exists = false;
    info->is_directory = false;
    info->size = 0U;
    const FatFsStorageStatus validation = ValidateFatFsAbsolutePath(path);
    if (validation != FatFsStorageStatus::Ok) return validation;
#if BMX_UPDATE_HAS_FATFS
    return StatValidated(path, info);
#else
    return FatFsStorageStatus::BackendUnavailable;
#endif
}

FatFsStorageStatus FatFsRemoveFileIfPresent(const char *path)
{
    FatFsPathInfo info;
    FatFsStorageStatus status = FatFsStatPath(path, &info);
    if (status != FatFsStorageStatus::Ok || !info.exists) return status;
    if (info.is_directory) return FatFsStorageStatus::IsDirectory;
#if BMX_UPDATE_HAS_FATFS
    return f_unlink(path) == FR_OK ? FatFsStorageStatus::Ok
                                   : FatFsStorageStatus::RemoveFailed;
#else
    return FatFsStorageStatus::BackendUnavailable;
#endif
}

FatFsDownloadSink::FatFsDownloadSink()
    : open_(false), expected_size_(0U), bytes_written_(0U),
      expected_sha256_(), streaming_sha256_(), part_path_(), final_path_()
{
#if BMX_UPDATE_HAS_FATFS
    memset(&file_, 0, sizeof(file_));
#endif
}

FatFsDownloadSink::~FatFsDownloadSink()
{
    Abort(false);
}

FatFsStorageStatus FatFsDownloadSink::Start(
    const char *part_path, const char *final_path, uint64_t expected_size,
    const uint8_t expected_sha256[kSha256DigestBytes])
{
    if (open_) return FatFsStorageStatus::Busy;
    if (expected_sha256 == 0) {
        return FatFsStorageStatus::InvalidArgument;
    }
    FatFsStorageStatus status = ValidateFatFsAbsolutePath(part_path);
    if (status != FatFsStorageStatus::Ok) return status;
    status = ValidateFatFsAbsolutePath(final_path);
    if (status != FatFsStorageStatus::Ok) return status;
    if (!EndsWith(part_path, ".part") || strcmp(part_path, final_path) == 0 ||
        !SameVolume(part_path, final_path) ||
        !CopyPath(part_path, part_path_, sizeof(part_path_)) ||
        !CopyPath(final_path, final_path_, sizeof(final_path_))) {
        return FatFsStorageStatus::InvalidPath;
    }
#if BMX_UPDATE_HAS_FATFS
    FatFsPathInfo final_info;
    status = FatFsStatPath(final_path_, &final_info);
    if (status != FatFsStorageStatus::Ok) return status;
    if (final_info.exists) return FatFsStorageStatus::AlreadyExists;
    status = FatFsRemoveFileIfPresent(part_path_);
    if (status != FatFsStorageStatus::Ok) return status;
    status = EnsureParentDirectories(part_path_);
    if (status != FatFsStorageStatus::Ok) return status;
    memset(&file_, 0, sizeof(file_));
    if (f_open(&file_, part_path_, FA_WRITE | FA_CREATE_NEW) != FR_OK) {
        return FatFsStorageStatus::OpenFailed;
    }
    open_ = true;
    expected_size_ = expected_size;
    bytes_written_ = 0U;
    memcpy(expected_sha256_, expected_sha256, sizeof(expected_sha256_));
    streaming_sha256_.Reset();
    return FatFsStorageStatus::Ok;
#else
    (void)expected_size;
    return FatFsStorageStatus::BackendUnavailable;
#endif
}

bool FatFsDownloadSink::Write(const uint8_t *data, size_t size)
{
    if (!open_) {
        return false;
    }
    if ((data == 0 && size != 0U) ||
        static_cast<uint64_t>(size) > expected_size_ - bytes_written_) {
        return false;
    }
#if BMX_UPDATE_HAS_FATFS
    FatFsStorageStatus status = WriteAll(&file_, data, size);
    if (status != FatFsStorageStatus::Ok) {
        return false;
    }
    if (!streaming_sha256_.Update(data, size)) {
        return false;
    }
    bytes_written_ += size;
    return true;
#else
    (void)data;
    (void)size;
    return false;
#endif
}

FatFsStorageStatus FatFsDownloadSink::FinishStreamingVerified()
{
    if (!open_) return FatFsStorageStatus::NotOpen;
#if BMX_UPDATE_HAS_FATFS
    if (bytes_written_ != expected_size_) {
        (void)f_close(&file_);
        open_ = false;
        return FatFsStorageStatus::SizeMismatch;
    }
    uint8_t streaming_digest[kSha256DigestBytes];
    if (!streaming_sha256_.Final(streaming_digest)) {
        (void)f_close(&file_);
        open_ = false;
        return FatFsStorageStatus::HashFailed;
    }
    FatFsStorageStatus status = FatFsStorageStatus::Ok;
    if (f_sync(&file_) != FR_OK) status = FatFsStorageStatus::SyncFailed;
    const FatFsStorageStatus close_status = CloseFile(&file_);
    open_ = false;
    if (status == FatFsStorageStatus::Ok) status = close_status;
    if (status != FatFsStorageStatus::Ok) return status;
    if (!ConstantTimeDigestEqual(streaming_digest, expected_sha256_)) {
        return FatFsStorageStatus::HashMismatch;
    }

    FatFsPathInfo final_info;
    status = FatFsStatPath(final_path_, &final_info);
    if (status != FatFsStorageStatus::Ok) return status;
    if (final_info.exists) return FatFsStorageStatus::AlreadyExists;
    if (f_rename(part_path_, final_path_) != FR_OK) {
        return FatFsStorageStatus::RenameFailed;
    }
    return FatFsStorageStatus::Ok;
#else
    return FatFsStorageStatus::BackendUnavailable;
#endif
}

void FatFsDownloadSink::Abort(bool remove_part)
{
#if BMX_UPDATE_HAS_FATFS
    if (open_) {
        (void)f_close(&file_);
        open_ = false;
    }
    if (remove_part && part_path_[0] != 0) {
        (void)FatFsRemoveFileIfPresent(part_path_);
    }
#else
    (void)remove_part;
#endif
}

FatFsZipSource::FatFsZipSource()
    : open_(false), size_(0U), position_(0U), positioned_(false)
{
#if BMX_UPDATE_HAS_FATFS
    memset(&file_, 0, sizeof(file_));
#endif
}

FatFsZipSource::~FatFsZipSource()
{
    (void)Close();
}

FatFsStorageStatus FatFsZipSource::Open(const char *path)
{
    if (open_) return FatFsStorageStatus::Busy;
    FatFsStorageStatus status = ValidateFatFsAbsolutePath(path);
    if (status != FatFsStorageStatus::Ok) return status;
#if BMX_UPDATE_HAS_FATFS
    FatFsPathInfo info;
    status = FatFsStatPath(path, &info);
    if (status != FatFsStorageStatus::Ok) return status;
    if (!info.exists) return FatFsStorageStatus::NotFound;
    if (info.is_directory) return FatFsStorageStatus::IsDirectory;
    memset(&file_, 0, sizeof(file_));
    if (f_open(&file_, path, FA_READ | FA_OPEN_EXISTING) != FR_OK) {
        return FatFsStorageStatus::OpenFailed;
    }
    size_ = info.size;
    if (static_cast<uint64_t>(f_size(&file_)) != size_) {
        (void)f_close(&file_);
        return FatFsStorageStatus::SizeMismatch;
    }
    open_ = true;
    position_ = 0U;
    positioned_ = true;  // f_open positions at offset 0
    return FatFsStorageStatus::Ok;
#else
    return FatFsStorageStatus::BackendUnavailable;
#endif
}

FatFsStorageStatus FatFsZipSource::Close()
{
    if (!open_) return FatFsStorageStatus::Ok;
#if BMX_UPDATE_HAS_FATFS
    open_ = false;
    positioned_ = false;
    return CloseFile(&file_);
#else
    open_ = false;
    return FatFsStorageStatus::BackendUnavailable;
#endif
}

bool FatFsZipSource::GetSize(uint64_t *size)
{
    if (!open_ || size == 0) {
        return false;
    }
#if BMX_UPDATE_HAS_FATFS
    if (static_cast<uint64_t>(f_size(&file_)) != size_) {
        return false;
    }
    *size = size_;
    return true;
#else
    return false;
#endif
}

bool FatFsZipSource::ReadAt(uint64_t offset, uint8_t *destination, size_t size)
{
    if (!open_ || (destination == 0 && size != 0U) || offset > size_ ||
        static_cast<uint64_t>(size) > size_ - offset) {
        return false;
    }
#if BMX_UPDATE_HAS_FATFS
    const FSIZE_t fat_offset = static_cast<FSIZE_t>(offset);
    if (static_cast<uint64_t>(fat_offset) != offset) {
        return false;
    }
    if (!positioned_ || offset != position_) {
        if (f_lseek(&file_, fat_offset) != FR_OK ||
            static_cast<uint64_t>(f_tell(&file_)) != offset) {
            positioned_ = false;
            return false;
        }
        position_ = offset;
        positioned_ = true;
    }
    const FatFsStorageStatus status = ReadExact(&file_, destination, size);
    if (status != FatFsStorageStatus::Ok) {
        positioned_ = false;
        return false;
    }
    position_ = offset + static_cast<uint64_t>(size);
    return true;
#else
    (void)destination;
    (void)size;
    return false;
#endif
}

}  // namespace update
}  // namespace bmx
