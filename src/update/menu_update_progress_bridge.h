#ifndef BMX_UPDATE_MENU_UPDATE_PROGRESS_BRIDGE_H
#define BMX_UPDATE_MENU_UPDATE_PROGRESS_BRIDGE_H

#include "update/update_foreground_progress.h"

namespace bmx {
namespace update {

// Returns null unless the explicit menu action currently owns the modal
// progress session.  This makes accidental boot/background composition inert.
UpdateForegroundProgress *ActiveMenuUpdateForegroundProgress();

}  // namespace update
}  // namespace bmx

#ifdef __cplusplus
extern "C" {
#endif

int emux_update_progress_begin_explicit(void);
void emux_update_progress_end_explicit(void);

#ifdef __cplusplus
}
#endif

#endif  // BMX_UPDATE_MENU_UPDATE_PROGRESS_BRIDGE_H
