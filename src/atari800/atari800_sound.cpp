//
// atari800_sound.cpp
//
// Atari800 audio output for BMX bare-metal.
//
// atari800's libatari800 path fills a per-frame PCM buffer
// (LIBATARI800_Sound_array, conventionally 16-bit signed) with
// Sound_out.channels interleaved channels. BMX owns audio playback on core 1
// via the same circle_sound_write() path the C64 kernels use, so we hand each
// frame's PCM straight to that sink.
//

#include "atari800_sound.h"
#include "atari800_bridge.h"
#include "atari800_debug.h"

#include "defs.h"

extern "C" {
#include "third_party/atari800/src/config.h"
#include "third_party/atari800/src/sound.h"
#include "third_party/atari800/src/libatari800/libatari800.h"
#include "third_party/common/circle.h"
}

#include <string.h>

namespace bmx {
namespace atari800 {

static bool s_running = false;

// Diagnostics: accumulated per sound_play call, reported periodically.
static unsigned long s_dbg_calls = 0;
static unsigned long s_dbg_bytes = 0;
static unsigned int s_dbg_max_amp = 0;
static int s_dbg_last_len = 0;
static int s_dbg_last_ch = 0;
static int s_dbg_last_size = 0;
static long s_dbg_last_written = 0;
static int s_dbg_last_space = 0;

void sound_init(void) {
  // Configure atari800 to render signed PCM at BMX's sample rate from the
  // staged sound choices (stereo/latency). Playback itself is started by
  // the kernel sound lifecycle like the other emulated machines.
  // Sound_Setup() leaves the mixer paused (paused=TRUE); the init path
  // in atari.c pairs it with Sound_Continue(), so apply_sound_setup()
  // does the same, or Sound_Update() renders zero bytes forever.
  s_running = apply_sound_setup();
  debug_log("Atari800 sound_init: running=%d freq=%d ch=%d", s_running ? 1 : 0,
            (int)Sound_desired.freq, (int)Sound_desired.channels);
}

static void note_samples(const int16_t *pcm, size_t count) {
  unsigned int peak = 0;
  for (size_t i = 0; i < count; i++) {
    int v = pcm[i] < 0 ? -pcm[i] : pcm[i];
    if (v > (int)peak) {
      peak = (unsigned int)v;
    }
    if (peak >= 32767) {
      break;
    }
  }
  if (peak > s_dbg_max_amp) {
    s_dbg_max_amp = peak;
  }
}

static unsigned long s_dbg_invocations = 0;
static unsigned long s_dbg_skip_disabled = 0;
static unsigned long s_dbg_skip_empty = 0;

void sound_play(void) {
  s_dbg_invocations++;
  if (!s_running || !Sound_enabled) {
    s_dbg_skip_disabled++;
    if ((s_dbg_invocations % 300) == 0) {
      debug_log("Atari800 sound: invocations=%lu skip_disabled=%lu "
                "running=%d enabled=%d",
                s_dbg_invocations, s_dbg_skip_disabled, s_running ? 1 : 0,
                Sound_enabled ? 1 : 0);
    }
    return;
  }

  int sample_size = libatari800_get_sound_sample_size();
  int channels = libatari800_get_num_sound_channels();
  int bytes = libatari800_get_sound_buffer_len();
  s_dbg_last_len = bytes;
  s_dbg_last_ch = channels;
  s_dbg_last_size = sample_size;
  if (sample_size == 0 || channels == 0 || bytes <= 0) {
    s_dbg_skip_empty++;
    return;
  }

  // We configure 16-bit mono, but downmix defensively if the core ever
  // produces stereo or 8-bit (some sound paths do).
  if (sample_size == 1) {
    const uint8_t *u8 = (const uint8_t *)libatari800_get_sound_buffer();
    size_t frames = (size_t)bytes / (size_t)channels;
    // 8-bit atari PCM is unsigned; convert to signed 16-bit mono.
    // Bound the stack buffer to one frame batch (libatari800 emits small
    // per-frame chunks; fall back to truncating absurd sizes).
    static int16_t mono[8192];
    if (frames > 8192) frames = 8192;
    for (size_t i = 0; i < frames; i++) {
      int32_t acc = 0;
      for (int c = 0; c < channels; c++) {
        acc += (int32_t)u8[i * (size_t)channels + (size_t)c] - 128;
      }
      mono[i] = (int16_t)((acc * 256) / (channels ? channels : 1));
    }
    note_samples(mono, frames);
    s_dbg_calls++;
    s_dbg_bytes += (unsigned long)bytes;
    s_dbg_last_written = (long)circle_sound_write(mono, frames);
    s_dbg_last_space = circle_sound_bufferspace();
    return;
  }

  const int16_t *samples = (const int16_t *)libatari800_get_sound_buffer();
  if (channels == 1) {
    size_t count = (size_t)bytes / 2;
    note_samples(samples, count);
    s_dbg_calls++;
    s_dbg_bytes += (unsigned long)bytes;
    s_dbg_last_written = (long)circle_sound_write((int16_t *)samples, count);
    s_dbg_last_space = circle_sound_bufferspace();
  } else {
    // Stereo (or more) 16-bit: average to mono.
    size_t frames = (size_t)bytes / (size_t)(2 * channels);
    static int16_t mono16[8192];
    if (frames > 8192) frames = 8192;
    for (size_t i = 0; i < frames; i++) {
      int32_t acc = 0;
      for (int c = 0; c < channels; c++) {
        acc += samples[i * (size_t)channels + (size_t)c];
      }
      mono16[i] = (int16_t)(acc / (channels ? channels : 1));
    }
    note_samples(mono16, frames);
    s_dbg_calls++;
    s_dbg_bytes += (unsigned long)bytes;
    s_dbg_last_written = (long)circle_sound_write(mono16, frames);
    s_dbg_last_space = circle_sound_bufferspace();
  }
}

void sound_debug_snapshot(unsigned long *invocations,
                          unsigned long *skip_disabled,
                          unsigned long *skip_empty, unsigned long *calls,
                          unsigned long *bytes, unsigned int *peak,
                          int *last_len, int *last_ch, int *last_size,
                          long *written, int *space, int *enabled) {
  if (invocations) *invocations = s_dbg_invocations;
  if (skip_disabled) *skip_disabled = s_dbg_skip_disabled;
  if (skip_empty) *skip_empty = s_dbg_skip_empty;
  if (calls) *calls = s_dbg_calls;
  if (bytes) {
    *bytes = s_dbg_bytes;
    s_dbg_bytes = 0;
  }
  if (peak) {
    *peak = s_dbg_max_amp;
    s_dbg_max_amp = 0;
  }
  if (last_len) *last_len = s_dbg_last_len;
  if (last_ch) *last_ch = s_dbg_last_ch;
  if (last_size) *last_size = s_dbg_last_size;
  if (written) *written = s_dbg_last_written;
  if (space) *space = s_dbg_last_space;
  if (enabled) *enabled = Sound_enabled ? 1 : 0;
}

}  // namespace atari800
}  // namespace bmx
