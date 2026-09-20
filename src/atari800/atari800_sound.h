//
// atari800_sound.h
//
// Atari800 audio output for BMX bare-metal
//

#ifndef atari800_sound_h
#define atari800_sound_h

namespace bmx {
namespace atari800 {

void sound_init(void);
void sound_play(void);

// Source-side audio diagnostics snapshot for the frame-loop heartbeat.
// Windowed byte/peak counters reset on read; invocation/skip totals run
// since boot.
void sound_debug_snapshot(unsigned long *invocations,
                          unsigned long *skip_disabled,
                          unsigned long *skip_empty, unsigned long *calls,
                          unsigned long *bytes, unsigned int *peak,
                          int *last_len, int *last_ch, int *last_size,
                          long *written, int *space, int *enabled);

}  // namespace atari800
}  // namespace bmx

#endif
