#include "update_policy.h"

namespace bmx {
namespace update {

UpdateStartDecision EvaluateUpdateStart(const UpdateStartContext &context)
{
    if (context.invocation != UpdateInvocation::ConfirmedUpdateMenuAction) {
        return UpdateStartDecision::NotExplicitUserAction;
    }
    if (!context.network_feature_enabled) {
        return UpdateStartDecision::NetworkFeatureDisabled;
    }
    if (!context.network_ready) {
        return UpdateStartDecision::NetworkNotReady;
    }
    return UpdateStartDecision::Allowed;
}

ReleaseDecision EvaluateReleaseCandidate(const InstalledRelease &installed,
                                         const ReleaseCandidate &candidate)
{
    if (!installed.has_update_metadata) {
        return ReleaseDecision::LegacyInstallation;
    }
    if (!IsKnownBoardFamily(installed.board) || installed.release_sequence == 0U ||
        installed.updater_abi == 0U) {
        return ReleaseDecision::InvalidInstalledMetadata;
    }
    if (candidate.source != ReleaseSource::GitHubLatestKdreBmx) {
        return ReleaseDecision::UnsupportedSource;
    }
    if (candidate.channel != ReleaseChannel::Stable || candidate.api_reports_draft ||
        candidate.api_reports_prerelease) {
        return ReleaseDecision::NotStable;
    }
    if (!IsKnownBoardFamily(candidate.board) || candidate.release_sequence == 0U ||
        candidate.minimum_updater_abi == 0U || candidate.maximum_updater_abi == 0U ||
        candidate.minimum_updater_abi > candidate.maximum_updater_abi) {
        return ReleaseDecision::InvalidCandidateMetadata;
    }
    if (candidate.board != installed.board) {
        return ReleaseDecision::BoardMismatch;
    }
    if (installed.updater_abi < candidate.minimum_updater_abi ||
        installed.updater_abi > candidate.maximum_updater_abi) {
        return ReleaseDecision::UpdaterAbiMismatch;
    }
    if (candidate.release_sequence == installed.release_sequence) {
        return ReleaseDecision::AlreadyCurrent;
    }
    if (candidate.release_sequence < installed.release_sequence) {
        return ReleaseDecision::DowngradeDenied;
    }
    return ReleaseDecision::UpdateAvailable;
}

}  // namespace update
}  // namespace bmx
