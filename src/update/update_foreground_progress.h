#ifndef BMX_UPDATE_UPDATE_FOREGROUND_PROGRESS_H
#define BMX_UPDATE_UPDATE_FOREGROUND_PROGRESS_H

#include "update/update_types.h"

namespace bmx {
namespace update {

enum class UpdateForegroundPhase : uint8_t {
    Discovery = 0,
    Manifest,
    Zip,
    Hash,
    Stage,
    Reboot
};

enum class UpdateForegroundCancelBehavior : uint8_t {
    CancelAtCheckpoint = 0,
    MandatoryCompletion
};

struct UpdateForegroundUiEvent {
    UpdateForegroundPhase phase;
    uint16_t progress_per_mille;
    bool determinate;
    bool cancel_enabled;
    bool cancel_pending;
};

class UpdateForegroundUi {
public:
    virtual ~UpdateForegroundUi() {}
    virtual bool Begin() = 0;
    virtual void Present(const UpdateForegroundUiEvent &event) = 0;
    virtual bool PumpAndReadCancel() = 0;
    virtual void End() = 0;
};

// One instance belongs to one explicitly selected Update menu action. The
// caller supplies one monotonic transaction-wide counter per visible phase.
class UpdateForegroundProgress {
public:
    explicit UpdateForegroundProgress(UpdateForegroundUi *ui);

    bool BeginExplicit();
    void EndExplicit();
    bool Checkpoint(
        UpdateForegroundPhase phase,
        uint64_t completed_bytes = 0U,
        uint64_t total_bytes = 0U,
        UpdateForegroundCancelBehavior behavior =
            UpdateForegroundCancelBehavior::CancelAtCheckpoint);

    bool active() const { return active_; }
    bool cancel_requested() const { return cancel_requested_; }
    UpdateForegroundPhase phase() const { return phase_; }

private:
    UpdateForegroundProgress(const UpdateForegroundProgress &);
    UpdateForegroundProgress &operator=(const UpdateForegroundProgress &);

    UpdateForegroundUi *ui_;
    UpdateForegroundPhase phase_;
    bool active_;
    bool cancel_requested_;
};

const char *UpdateForegroundPhaseString(UpdateForegroundPhase phase);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_UPDATE_FOREGROUND_PROGRESS_H
