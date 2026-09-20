//
// atari800_menu.h
//
// Atari800 menu integration for BMX bare-metal
//

#ifndef atari800_menu_h
#define atari800_menu_h

namespace bmx {
namespace atari800 {

// Initializes the atari800 menu for the current machine. No-op for now; the
// atari800 text menu will be adapted to the BMX FBL in a later phase.
void menu_init(void);

}  // namespace atari800
}  // namespace bmx

#endif
