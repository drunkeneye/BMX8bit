//
// atari800_video.h
//
// Atari800 video presentation for BMX bare-metal
//

#ifndef atari800_video_h
#define atari800_video_h

namespace bmx {
namespace atari800 {

void video_init(void);
void video_render(void);

// Monitor emulation (Color / B&W / Green / Amber). Applied as a transform
// on Colours_table at palette upload, so it tracks tint/sat sliders.
enum {
  ATARI_MONITOR_COLOR = 0,
  ATARI_MONITOR_BW = 1,
  ATARI_MONITOR_GREEN = 2,
  ATARI_MONITOR_AMBER = 3,
};
void video_set_monitor(int mode);
int video_get_monitor(void);
// Force-reuploads the palette with the current monitor transform.
void video_refresh_palette(void);
// Dumps the current emulator frame (full 384x240 + palette with monitor
// transform) as uncompressed 24-bit BMP. Returns true on success.
bool video_save_screenshot(const char *path);

// Present-path diagnostics for the heartbeat line.
void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us);

}  // namespace atari800
}  // namespace bmx

#endif
