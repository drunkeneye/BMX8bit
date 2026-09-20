#ifndef BMX_UPDATE_ZIP_READER_H
#define BMX_UPDATE_ZIP_READER_H

#include "update_types.h"

namespace bmx {
namespace update {

// bmx-zip32-v1 deliberately caps all metadata so the device parser can be
// allocation-free. Callers provide one ZipEntry slot per central entry.
// Raised for archive browsing (complete game packs hold 30k+ files); the
// updater's heap array grows accordingly (~10MB transient, Pi-fine).
static const size_t kZipMaximumEntries = 32768U;
static const size_t kZipMaximumPathBytes = 240U;
// 32 KiB source chunks: one FatFS seek+read per chunk instead of four.
// The workspace stays caller-owned (static or heap), never on the task stack.
static const size_t kZipInputBufferBytes = 32768U;
static const size_t kZipInflateDictionaryBytes = 32768U;
static const size_t kZipInflateStateWords = 1056U;

// Roughly 73 KiB, intentionally caller-owned so the raw-Deflate dictionary is
// never placed on Circle's default 32 KiB task stack. One reader operation may
// use a workspace at a time.
struct ZipWorkspace {
    uint8_t input[kZipInputBufferBytes];
    uint8_t dictionary[kZipInflateDictionaryBytes];
    uint64_t inflate_state[kZipInflateStateWords];
};

struct ZipLimits {
    uint64_t maximum_archive_bytes;
    size_t maximum_entries;
    size_t maximum_path_bytes;
    uint64_t maximum_file_bytes;
    uint64_t maximum_installed_bytes;
    uint32_t maximum_compression_ratio;

    ZipLimits();
};

enum class ZipCompression : uint8_t {
    Store = 0,
    Deflate = 8
};

struct ZipEntry {
    // Directories are stored without their ZIP trailing slash. Every path is
    // validated before becoming observable through this structure.
    char path[kZipMaximumPathBytes + 1U];
    bool is_directory;
    ZipCompression compression;
    uint16_t flags;
    uint32_t crc32;
    uint64_t compressed_size;
    uint64_t size;
    uint64_t local_header_offset;
    uint64_t data_offset;
    uint32_t external_attributes;
    uint8_t creator_system;
};

struct ZipInventory {
    const ZipEntry *entries;
    size_t entry_count;
    size_t file_count;
    size_t directory_count;
    uint64_t archive_size;
    uint64_t installed_size;
    uint64_t central_directory_offset;
    uint64_t central_directory_size;
};

struct ZipExpectedFile {
    const char *path;
    uint64_t size;
    ZipCompression compression;
    // NULL skips SHA-256 comparison for this file. If any digest is supplied,
    // the operation requires a ZipHashSink.
    const uint8_t *sha256;
};

struct ZipExpectedInventory {
    const ZipExpectedFile *files;
    size_t file_count;
    const char *const *directories;
    size_t directory_count;
};

// Implement this over a seekable file, block device, or host byte vector.
// ReadAt must either fill the complete requested range or return false.
class SeekableZipSource {
public:
    virtual ~SeekableZipSource() {}
    virtual bool GetSize(uint64_t *size) = 0;
    virtual bool ReadAt(uint64_t offset, uint8_t *destination, size_t size) = 0;
};

// Injected SHA-256 implementation. The reader owns neither the object nor any
// returned digest. One instance is reused sequentially, never concurrently.
class ZipHashSink {
public:
    virtual ~ZipHashSink();
    virtual bool BeginFile(const char *validated_path, uint64_t size) = 0;
    virtual bool Update(ByteView bytes) = 0;
    virtual bool FinishFile(uint8_t digest[kSha256DigestBytes]) = 0;
    virtual void AbortFile() = 0;
};

// Progress/cancel hook for long streaming passes. Called with produced and
// total uncompressed bytes; return false to abort (the operation fails with
// ZipStatus::Cancelled and the sink's AbortEntry runs). Null disables.
typedef bool (*ZipProgressCallback)(uint64_t produced_bytes,
                                    uint64_t total_bytes, void *context);

// The ZIP reader never opens filesystem paths. It passes only paths accepted
// by bmx-zip32-v1 to this sink.
class ZipExtractSink {
public:
    virtual ~ZipExtractSink();
    virtual bool BeginEntry(const ZipEntry &entry) = 0;
    virtual bool Write(ByteView bytes) = 0;
    virtual bool CommitEntry(const ZipEntry &entry) = 0;
    virtual void AbortEntry(const ZipEntry &entry) = 0;
};

enum class ZipStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    NotOpen,
    SourceIo,
    ArchiveSizeInvalid,
    EndRecordInvalid,
    ArchiveCommentForbidden,
    MultiDiskForbidden,
    Zip64Forbidden,
    EntryCountInvalid,
    StorageTooSmall,
    CentralDirectoryInvalid,
    UnsupportedVersion,
    UnsupportedFlags,
    UnsupportedCompression,
    ExtraFieldForbidden,
    MemberCommentForbidden,
    SpecialFileForbidden,
    DirectoryMetadataInvalid,
    PathInvalid,
    PathTooLong,
    PathEncodingInvalid,
    NonAsciiPathUnsupported,
    DuplicatePath,
    FatCaseCollision,
    FileUsedAsDirectory,
    MemberSizeInvalid,
    CompressionRatioInvalid,
    OffsetInvalid,
    LocalHeaderMismatch,
    InstalledSizeInvalid,
    ExpectedInventoryInvalid,
    InventoryMismatch,
    EntryNotFound,
    HashSinkRequired,
    HashSinkError,
    HashMismatch,
    SinkError,
    WorkspaceRequired,
    CrcMismatch,
    UncompressedSizeMismatch,
    DeflateInvalid,
    // Appended last: never renumber existing codes (persisted/logged).
    Cancelled
};

class ZipReader {
public:
    ZipReader();

    void SetWorkspace(ZipWorkspace *workspace);

    // Opt-in leniency for browsing user archives: skip (bounds-checked)
    // extra fields instead of rejecting the archive. Path/CRC/size and all
    // other validations stay strict. Never enabled by the update flow.
    void SetLenientExtraFields(bool lenient);

    // Opt-in path leniency for browsing: backslashes become separators and
    // leading slashes are stripped (Windows-origin archives), then all
    // normal validations apply. Never enabled by the update flow.
    void SetLenientPaths(bool lenient);

    // Opt-in archive leniency for browsing: ignore the trailing archive
    // comment (already bounds-validated, never parsed). Member comments
    // stay forbidden. Never enabled by the update flow.
    void SetLenientArchive(bool lenient);

    // Opt-in compression-ratio leniency for browsing: skip the zip-bomb
    // ratio cap at open. Browse extraction streams single files (bounded
    // memory; a runaway output fails visibly at the sink), while the
    // update flow keeps the strict cap. Never enabled by the update flow.
    void SetLenientRatio(bool lenient);

    // Raw offending name of the last validation failure (sanitized,
    // best effort; empty if none). Valid until the next Open call.
    const char *FirstFailurePath() const;

    // Open validates every EOCD, central, local, metadata, path, ordering, and
    // limit invariant, but does not yet inflate payloads. entries must remain
    // alive until this reader is destroyed or Open is called again. The
    // optional progress hook (produced = central entries scanned) keeps
    // huge archives cancellable; null disables.
    ZipStatus Open(SeekableZipSource *source,
                   ZipEntry *entries,
                   size_t entry_capacity,
                   const ZipLimits &limits = ZipLimits(),
                   ZipProgressCallback open_progress = 0,
                   void *open_context = 0);

    const ZipInventory &inventory() const;

    // Checks an exact (order-independent) manifest inventory, inflates every
    // file, and verifies its declared CRC-32, size, and optional SHA-256.
    ZipStatus ValidateContents(const ZipExpectedInventory &expected,
                               ZipHashSink *hash_sink = 0);

    // Binds an exact inventory without inflating every member. This is safe
    // only after the caller has authenticated the complete archive bytes
    // against the signed asset hash. Extracted files still verify CRC, size,
    // and SHA-256 while being written.
    ZipStatus BindAuthenticatedInventory(
        const ZipExpectedInventory &expected);

    // Extraction is normally two-pass: requested payloads are fully validated
    // before BeginEntry is called, then validated again while writing. A
    // successful ValidateContents call on this reader may satisfy the first
    // pass for the exact same inventory. The writing pass always verifies CRC,
    // size, and every supplied SHA-256 before CommitEntry. The source must
    // remain immutable for the duration of the operation.
    ZipStatus ExtractOne(const char *path,
                          const ZipExpectedInventory &expected,
                          ZipExtractSink *sink,
                          ZipHashSink *hash_sink = 0);
    // Single-pass variant for interactive browsing: the payload is inflated
    // once, straight into the sink, while CRC-32 and size are verified before
    // CommitEntry runs. On any failure (including Cancelled) CommitEntry never
    // runs and AbortEntry does, so the caller can simply delete the partial
    // output. Safe only because the browse caller removes the file unless the
    // call returns Ok. The update flow keeps using two-pass ExtractOne.
    // Separate progress hooks: verify (produced = files checked) keeps huge
    // manifests alive and cancellable; stream (produced = bytes written)
    // drives the progress bar. Either may be null.
    ZipStatus ExtractOneSinglePass(const char *path,
                                   const ZipExpectedInventory &expected,
                                   ZipExtractSink *sink,
                                   ZipProgressCallback verify_progress,
                                   void *verify_context,
                                   ZipProgressCallback stream_progress,
                                   void *stream_context);
    ZipStatus ExtractAll(const ZipExpectedInventory &expected,
                         ZipExtractSink *sink,
                         ZipHashSink *hash_sink = 0);

private:
    ZipReader(const ZipReader &);
    ZipReader &operator=(const ZipReader &);

    ZipStatus VerifyExpectedInventory(const ZipExpectedInventory &expected,
                                       bool *requires_hash) const;
    // Same check with progress reports (produced = files checked,
    // total = file count); false aborts with Cancelled. Used by the
    // interactive single-pass path so huge manifests stay cancellable;
    // the updater passes no callback.
    ZipStatus VerifyExpectedInventory(
        const ZipExpectedInventory &expected, bool *requires_hash,
        ZipProgressCallback progress, void *progress_context) const;
    ZipStatus StreamFile(const ZipEntry &entry,
                          const ZipExpectedFile *expected,
                          ZipHashSink *hash_sink,
                          ZipExtractSink *extract_sink,
                          ZipProgressCallback progress = 0,
                          void *progress_context = 0);
    const ZipExpectedFile *FindExpectedFile(
        const ZipExpectedInventory &expected,
        const char *path) const;
    bool ComputeExpectedInventoryBinding(
        const ZipExpectedInventory &expected,
        uint8_t digest[kSha256DigestBytes]) const;
    bool ValidatedContentsMatch(
        const ZipExpectedInventory &expected) const;
    void ClearValidatedContents();

    SeekableZipSource *source_;
    ZipEntry *entries_;
    ZipInventory inventory_;
    ZipLimits limits_;
    ZipWorkspace *workspace_;
    bool open_;
    bool lenient_extra_fields_;
    bool lenient_paths_;
    bool lenient_archive_;
    bool lenient_ratio_;
    char first_failure_path_[256];
    uint8_t validated_inventory_binding_[kSha256DigestBytes];
    bool validated_contents_;
};

const char *ZipStatusString(ZipStatus status);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_ZIP_READER_H
