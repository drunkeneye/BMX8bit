#include "update/menu_update_progress_bridge.h"

extern "C" {
#include "ui.h"
}

namespace bmx {
namespace update {
namespace {

class MenuUpdateForegroundUi : public UpdateForegroundUi {
public:
    bool Begin() override { return ui_update_progress_begin() != 0; }

    void Present(const UpdateForegroundUiEvent &event) override
    {
        ui_update_progress_present(
            static_cast<unsigned>(event.phase),
            static_cast<unsigned>(event.progress_per_mille),
            event.determinate ? 1 : 0,
            event.cancel_enabled ? 1 : 0,
            event.cancel_pending ? 1 : 0);
    }

    bool PumpAndReadCancel() override
    {
        return ui_update_progress_pump() != 0;
    }

    void End() override { ui_update_progress_end(); }
};

MenuUpdateForegroundUi g_menu_update_ui;
UpdateForegroundProgress g_menu_update_progress(&g_menu_update_ui);

}  // namespace

UpdateForegroundProgress *ActiveMenuUpdateForegroundProgress()
{
    return g_menu_update_progress.active() ? &g_menu_update_progress : 0;
}

}  // namespace update
}  // namespace bmx

extern "C" int emux_update_progress_begin_explicit(void)
{
    return bmx::update::g_menu_update_progress.BeginExplicit() ? 1 : 0;
}

extern "C" void emux_update_progress_end_explicit(void)
{
    bmx::update::g_menu_update_progress.EndExplicit();
}
