#ifndef BMX_UPDATE_UPDATE_POLICY_H
#define BMX_UPDATE_UPDATE_POLICY_H

#include "update_types.h"

namespace bmx {
namespace update {

// These values deliberately enumerate events which must never start discovery.
// Adding a new caller requires choosing an explicit cause; Unknown fails closed.
enum class UpdateInvocation : uint8_t {
    Unknown = 0,
    ConfirmedUpdateMenuAction = 1,
    Boot = 2,
    EmulatorStart = 3,
    NetworkEnabled = 4,
    NetworkBecameReady = 5,
    MenuOpened = 6,
    MenuIdle = 7,
    Timer = 8,
    LocalRecovery = 9
};

struct UpdateStartContext {
    UpdateInvocation invocation;
    bool network_feature_enabled;
    bool network_ready;
};

enum class UpdateStartDecision : uint8_t {
    Allowed = 0,
    NotExplicitUserAction,
    NetworkFeatureDisabled,
    NetworkNotReady
};

UpdateStartDecision EvaluateUpdateStart(const UpdateStartContext &context);

enum class ReleaseSource : uint8_t {
    Unknown = 0,
    GitHubLatestKdreBmx = 1,
    Other = 2
};

struct InstalledRelease {
    bool has_update_metadata;
    uint64_t release_sequence;
    BoardFamily board;
    uint32_t updater_abi;
};

struct ReleaseCandidate {
    ReleaseSource source;
    ReleaseChannel channel;
    bool api_reports_draft;
    bool api_reports_prerelease;
    uint64_t release_sequence;
    BoardFamily board;
    uint32_t minimum_updater_abi;
    uint32_t maximum_updater_abi;
};

enum class ReleaseDecision : uint8_t {
    UpdateAvailable = 0,
    LegacyInstallation,
    InvalidInstalledMetadata,
    UnsupportedSource,
    NotStable,
    InvalidCandidateMetadata,
    BoardMismatch,
    UpdaterAbiMismatch,
    AlreadyCurrent,
    DowngradeDenied
};

ReleaseDecision EvaluateReleaseCandidate(const InstalledRelease &installed,
                                         const ReleaseCandidate &candidate);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_UPDATE_POLICY_H
