#include "update/update_foreground_progress.h"

namespace bmx {
namespace update {

namespace {

uint16_t RatioPerMille(uint64_t completed, uint64_t total)
{
    if (total == 0U) return 0U;
    if (completed >= total) return 1000U;
    const uint64_t quotient = completed / total;
    const uint64_t remainder = completed % total;
    uint64_t scaled = quotient * 1000U;
    scaled += remainder <= UINT64_MAX / 1000U
        ? (remainder * 1000U) / total
        : remainder / (total / 1000U + 1U);
    return static_cast<uint16_t>(scaled > 1000U ? 1000U : scaled);
}

}  // namespace

UpdateForegroundProgress::UpdateForegroundProgress(UpdateForegroundUi *ui)
    : ui_(ui), phase_(UpdateForegroundPhase::Discovery), active_(false),
      cancel_requested_(false)
{
}

bool UpdateForegroundProgress::BeginExplicit()
{
    if (active_ || ui_ == 0 || !ui_->Begin()) return false;
    phase_ = UpdateForegroundPhase::Discovery;
    active_ = true;
    cancel_requested_ = false;
    return Checkpoint(UpdateForegroundPhase::Discovery);
}

void UpdateForegroundProgress::EndExplicit()
{
    if (active_ && ui_ != 0) ui_->End();
    active_ = false;
}

bool UpdateForegroundProgress::Checkpoint(
    UpdateForegroundPhase phase, uint64_t completed, uint64_t total,
    UpdateForegroundCancelBehavior behavior)
{
    if (!active_) return true;
    if (ui_ == 0) return false;
    phase_ = phase;
    UpdateForegroundUiEvent event;
    event.phase = phase;
    event.progress_per_mille = RatioPerMille(completed, total);
    event.determinate = total != 0U;
    event.cancel_enabled =
        behavior == UpdateForegroundCancelBehavior::CancelAtCheckpoint;
    event.cancel_pending = cancel_requested_;
    ui_->Present(event);
    if (ui_->PumpAndReadCancel()) cancel_requested_ = true;
    event.cancel_pending = cancel_requested_;
    if (cancel_requested_) ui_->Present(event);
    return !cancel_requested_ ||
           behavior == UpdateForegroundCancelBehavior::MandatoryCompletion;
}

const char *UpdateForegroundPhaseString(UpdateForegroundPhase phase)
{
    switch (phase) {
    case UpdateForegroundPhase::Discovery: return "discovery";
    case UpdateForegroundPhase::Manifest: return "manifest";
    case UpdateForegroundPhase::Zip: return "zip";
    case UpdateForegroundPhase::Hash: return "hash";
    case UpdateForegroundPhase::Stage: return "stage";
    case UpdateForegroundPhase::Reboot: return "reboot";
    }
    return "unknown";
}

}  // namespace update
}  // namespace bmx
