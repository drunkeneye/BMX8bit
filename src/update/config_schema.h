#ifndef BMX_UPDATE_CONFIG_SCHEMA_H
#define BMX_UPDATE_CONFIG_SCHEMA_H

#include <stddef.h>
#include <stdint.h>

namespace bmx {
namespace update {

static const size_t kMaximumConfigAreas = 16U;

enum class ConfigArea : uint8_t {
    Unknown = 0,
    Machines = 1,
    ConfigManagedBlock = 2,
    CmdlineManagedKeys = 3,
    Network = 4,
    Settings = 5,
    UpdateState = 6
};

bool IsKnownConfigArea(ConfigArea area);

enum class ConfigClassification : uint8_t {
    Compatible = 0,
    LosslessMigration,
    ResetRequired,
    UnknownOrCorrupt,
    NewerThanTarget
};

enum class ConfigPlanDecision : uint8_t {
    Compatible = 0,
    LosslessMigration,
    ResetConfirmationRequired,
    BlockedUnknownOrCorrupt,
    BlockedNewerThanTarget
};

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_CONFIG_SCHEMA_H
