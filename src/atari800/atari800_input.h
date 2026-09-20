//
// atari800_input.h
//
// Atari800 input (keyboard/joystick/mouse) for BMX bare-metal
//

#ifndef atari800_input_h
#define atari800_input_h

extern "C" {
#include "third_party/atari800/src/libatari800/libatari800.h"
}

namespace bmx {
namespace atari800 {

// Polls BMX queued input events and fills the libatari800 input template.
void poll_input(input_template_t *input);

}  // namespace atari800
}  // namespace bmx

#endif
