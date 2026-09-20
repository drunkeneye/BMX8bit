#ifndef BMX_UPDATE_CONFIG_MIGRATION_H
#define BMX_UPDATE_CONFIG_MIGRATION_H

#include "update/config_schema.h"
#include "update/update_types.h"

namespace bmx {
namespace update {

// Configuration assessment is independent of a filesystem. The caller reads
// a stable snapshot and passes bounded views here before the simple installer
// overwrites release-managed files.
static const size_t kConfigMigrationAreaCount = 6U;
static const size_t kMaximumConfigFilesPerArea = 16U;
static const size_t kMaximumConfigPathBytes = 127U;
static const size_t kMaximumConfigFileBytes = 512U * 1024U;
static const size_t kMaximumConfigAreaBytes = 2U * 1024U * 1024U;
static const size_t kMaximumDeclaredConfigMigrations = 64U;
static const size_t kMaximumConfigMigrationIdBytes = 64U;
struct ConfigFileView {
    // Relative boot-partition path, using '/'.  Only the fixed paths/families
    // belonging to the enclosing area are accepted.
    const char *path;
    ByteView content;
};

struct ConfigAreaSnapshot {
    ConfigArea area;
    const ConfigFileView *files;
    size_t file_count;
};

struct ConfigSnapshot {
    // A full snapshot contains every area exactly once.  An absent optional
    // area is represented by an area with zero files, never by omitting it.
    const ConfigAreaSnapshot *areas;
    size_t area_count;
};

struct ConfigSchemaRequirement {
    ConfigArea area;
    uint32_t target_version;
    const uint32_t *accepted_versions;
    size_t accepted_version_count;
};

struct DeclaredConfigMigration {
    const char *id;
    ConfigArea area;
    uint32_t from_version;
    uint32_t to_version;
    bool lossy;
};

enum class ConfigMigrationRegistryStatus : uint8_t {
    Valid = 0,
    InvalidInput,
    DuplicateDeclaration,
    UnknownMigration,
    DeclarationMismatch,
    SchemaMismatch
};

// Every declaration from the signed manifest must match an exact compiled-in
// registry entry.  The manifest selects code; it never supplies code or a
// generic transformation language.
ConfigMigrationRegistryStatus ValidateConfigMigrationDeclarations(
    const ConfigSchemaRequirement *requirements,
    size_t requirement_count,
    const DeclaredConfigMigration *migrations,
    size_t migration_count);

enum class ConfigDetectionDetail : uint8_t {
    Valid = 0,
    Missing,
    UnknownFormat,
    Corrupt,
    LimitExceeded
};

struct ConfigAreaAssessment {
    ConfigArea area;
    ConfigDetectionDetail detection;
    uint32_t source_version;
    uint32_t target_version;
    ConfigClassification classification;
    // SHA-256 over path, length and exact bytes of every file in this area.
    // For config.txt/cmdline.txt this intentionally covers the complete file,
    // so any concurrent edit invalidates a previous UI consent.
    uint8_t source_content_sha256[kSha256DigestBytes];
};

struct ConfigMigrationPlan {
    ConfigAreaAssessment areas[kConfigMigrationAreaCount];
    size_t area_count;
    ConfigPlanDecision decision;
    size_t compatible_count;
    size_t migration_count;
    size_t reset_count;
    size_t blocked_count;
};

enum class ConfigAssessmentStatus : uint8_t {
    Ok = 0,
    InvalidInput,
    RegistryRejected,
    HashFailed
};

ConfigAssessmentStatus AssessConfigSnapshot(
    const ConfigSnapshot &snapshot,
    const ConfigSchemaRequirement *requirements,
    size_t requirement_count,
    const DeclaredConfigMigration *migrations,
    size_t migration_count,
    ConfigMigrationPlan *plan);

const char *ConfigAssessmentStatusString(ConfigAssessmentStatus status);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_CONFIG_MIGRATION_H
