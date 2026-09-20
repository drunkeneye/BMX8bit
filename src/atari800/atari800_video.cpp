//
// atari800_video.cpp
//
// Atari800 video presentation for BMX bare-metal.
//
// atari800 produces an indexed-color screen (Screen_atari, 384x240 bytes,
// each byte an Atari color index) plus a 256-entry RGB palette
// (Colours_table). The VIC FBL is a direct-color RGB565 layer. PAL frames
// go through the PAL delay-line blend (separate even/odd-line phases from
// COLOURS_PAL_GetYUV, mixed with the previous line exactly like upstream's
// PAL_BLENDING_Blit32); NTSC frames map Colours_table directly. The bundled
// 5.0.0 core only averages the even/odd phases into Colours_table ("not
// emulated"), which renders delay-line images (RastaConverter) red and
// streaky; the blend restores the hardware look.
//

#include "atari800_video.h"
#include "atari800_config.h"
#include "atari800_debug.h"

extern "C" {
#include "third_party/atari800/src/config.h"
#include "third_party/atari800/src/colours.h"
#include "third_party/atari800/src/colours_pal.h"
#include "third_party/atari800/src/screen.h"
#include "third_party/common/circle.h"
#include "third_party/common/emux_api.h"
}

#include <circle/timer.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace bmx {
namespace atari800 {

// Visible Atari region within the 384-pixel-wide screen. atari800 documents
// that nothing should be displayed outside the middle 336 columns; the side
// columns stay black (cleared at init, never written).
static const int kVisibleX1 = 24;
static const int kVisibleX2 = 360;  // 336 wide

static uint16_t rgb_to_565(uint32_t rgb) {
  uint32_t r = (rgb >> 16) & 0xff;
  uint32_t g = (rgb >> 8) & 0xff;
  uint32_t b = rgb & 0xff;
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static uint8_t *s_fb;
static int s_fb_pitch;

// Active monitor type (see header). Staged before video_init via settings.
static int s_monitor = ATARI_MONITOR_COLOR;

void video_set_monitor(int mode) {
  if (mode < ATARI_MONITOR_COLOR || mode > ATARI_MONITOR_AMBER) {
    return;
  }
  s_monitor = mode;
}

int video_get_monitor(void) { return s_monitor; }

// Luma-driven phosphor mapping: real green/amber monitors showed luma only.
static uint32_t monitor_transform(uint32_t rgb) {
  if (s_monitor == ATARI_MONITOR_COLOR) {
    return rgb;
  }
  uint32_t r = (rgb >> 16) & 0xff;
  uint32_t g = (rgb >> 8) & 0xff;
  uint32_t b = rgb & 0xff;
  uint32_t luma = (299 * r + 587 * g + 114 * b) / 1000;
  if (s_monitor == ATARI_MONITOR_BW) {
    return (luma << 16) | (luma << 8) | luma;
  }
  if (s_monitor == ATARI_MONITOR_GREEN) {
    return (luma << 8);
  }
  // Amber ~ #FFB000: G/R = 176/255.
  uint32_t amber_g = (luma * 176) / 255;
  return (luma << 16) | (amber_g << 8);
}

// -- PAL delay-line blend state (RGB888, even/odd line phases) ------------
//
// Same tables upstream's PAL_BLENDING_UpdateLookup builds: YUV with
// per-phase chroma, converted with the same gamma path. Rebuilt whenever
// Colours_table changes (Colours_Update regenerates both together).
static uint32_t s_blend_even[256];
static uint32_t s_blend_odd[256];
static int s_blend_shadow[256];
static bool s_blend_valid = false;
static unsigned s_frame_no = 0;

static uint32_t pack888(int r, int g, int b) {
  if (r < 0) r = 0;
  if (r > 255) r = 255;
  if (g < 0) g = 0;
  if (g > 255) g = 255;
  if (b < 0) b = 0;
  if (b > 255) b = 255;
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t yuv_to_rgb888(double y, double u, double v) {
  double r, g, b;
  Colours_YUV2RGB(y, u, v, &r, &g, &b);
  if (!COLOURS_PAL_external.loaded || COLOURS_PAL_external.adjust) {
    r = Colours_Gamma2Linear(r, COLOURS_PAL_setup.gamma);
    g = Colours_Gamma2Linear(g, COLOURS_PAL_setup.gamma);
    b = Colours_Gamma2Linear(b, COLOURS_PAL_setup.gamma);
    r = Colours_Linear2sRGB(r);
    g = Colours_Linear2sRGB(g);
    b = Colours_Linear2sRGB(b);
  }
  return pack888((int)(r * 255.0), (int)(g * 255.0), (int)(b * 255.0));
}

static void update_blend_tables(void) {
  double yuv[256 * 5];
  COLOURS_PAL_GetYUV(yuv);
  for (int i = 0; i < 256; i++) {
    double y = yuv[i * 5];
    double even_u = yuv[i * 5 + 1];
    double odd_u = yuv[i * 5 + 2];
    double even_v = yuv[i * 5 + 3];
    double odd_v = yuv[i * 5 + 4];
    s_blend_even[i] = yuv_to_rgb888(y, even_u, even_v);
    s_blend_odd[i] = yuv_to_rgb888(y, odd_u, odd_v);
  }
  memcpy(s_blend_shadow, ::Colours_table, sizeof(s_blend_shadow));
  s_blend_valid = true;
}

static void ensure_blend_tables(void) {
  if (!s_blend_valid ||
      memcmp(s_blend_shadow, ::Colours_table, sizeof(s_blend_shadow)) != 0) {
    update_blend_tables();
  }
}

// One blended pixel (Blit32 formula): current-line phase palette for the
// pixel, opposite-phase palette for previous-line hue + current luma
// (Atari index nibbles: high = hue, low = luma), averaged per channel.
static uint32_t render_pixel_pal(const uint8_t *src, int y, int x,
                                 int odd_frame) {
  uint32_t c = src[y * Screen_WIDTH + x];
  uint32_t p = (y > 0) ? src[(y - 1) * Screen_WIDTH + x] : c;
  uint32_t mixed = (p & 0xF0) | (c & 0x0F);
  const uint32_t *pal = odd_frame ? s_blend_odd : s_blend_even;
  const uint32_t *pal_prev = odd_frame ? s_blend_even : s_blend_odd;
  uint32_t a = pal[c];
  uint32_t b = pal_prev[mixed];
  uint32_t r = (((a >> 16) & 0xff) + ((b >> 16) & 0xff)) / 2;
  uint32_t g = (((a >> 8) & 0xff) + ((b >> 8) & 0xff)) / 2;
  uint32_t bl = ((a & 0xff) + (b & 0xff)) / 2;
  return (r << 16) | (g << 8) | bl;
}

// Displayed RGB888 for a visible-column pixel (blend in PAL, direct map in
// NTSC), before the monitor transform.
static uint32_t display_pixel(const uint8_t *src, int y, int x, bool blend,
                              int odd_frame) {
  if (blend) {
    return render_pixel_pal(src, y, x, odd_frame);
  }
  return (uint32_t)::Colours_table[src[y * Screen_WIDTH + x]];
}

// Renders Screen_atari into the RGB565 FBL (visible columns; sides stay
// black). No present: callers decide.
static void render_frame_buffer(void) {
  if (s_fb == nullptr || ::Screen_atari == nullptr) {
    return;
  }
  const uint8_t *src = (const uint8_t *)::Screen_atari;
  bool blend = !config_is_ntsc();
  if (blend) {
    ensure_blend_tables();
  }
  int start_odd = (s_frame_no & 1);
  for (int y = 0; y < Screen_HEIGHT; y++) {
    // The delay-line phase alternates every line, like upstream's blitter.
    int odd_line = start_odd ^ (y & 1);
    uint16_t *dst = (uint16_t *)(s_fb + (size_t)y * (size_t)s_fb_pitch);
    for (int x = kVisibleX1; x < kVisibleX2; x++) {
      dst[x] = rgb_to_565(
          monitor_transform(display_pixel(src, y, x, blend, odd_line)));
    }
  }
}

static void write_le16(FILE *fp, uint16_t v) {
  uint8_t b[2] = {(uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff)};
  fwrite(b, 1, 2, fp);
}

static void write_le32(FILE *fp, uint32_t v) {
  uint8_t b[4] = {(uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff),
                  (uint8_t)((v >> 16) & 0xff), (uint8_t)((v >> 24) & 0xff)};
  fwrite(b, 1, 4, fp);
}

bool video_save_screenshot(const char *path) {
  if (path == NULL || ::Screen_atari == NULL) {
    return false;
  }
  FILE *fp = fopen(path, "wb");
  if (fp == NULL) {
    return false;
  }
  // Uncompressed 24-bit BMP: opens everywhere, no encoder needed. Captures
  // the displayed pixels (PAL blend + monitor transform); side columns are
  // black like on screen. Rows are bottom-up BGR, padded to 4 bytes.
  static const int kBpp = 3;
  int row_stride = Screen_WIDTH * kBpp;
  int row_pad = (4 - (row_stride % 4)) % 4;
  uint32_t pixel_bytes =
      (uint32_t)(row_stride + row_pad) * (uint32_t)Screen_HEIGHT;
  // File header (14 bytes).
  fwrite("BM", 1, 2, fp);
  write_le32(fp, 54 + pixel_bytes);
  write_le32(fp, 0);
  write_le32(fp, 54);
  // Info header (40 bytes).
  write_le32(fp, 40);
  write_le32(fp, (uint32_t)Screen_WIDTH);
  write_le32(fp, (uint32_t)Screen_HEIGHT);
  write_le16(fp, 1);
  write_le16(fp, 24);
  write_le32(fp, 0);
  write_le32(fp, pixel_bytes);
  write_le32(fp, 0);
  write_le32(fp, 0);
  write_le32(fp, 0);
  write_le32(fp, 0);
  const uint8_t *src = (const uint8_t *)::Screen_atari;
  bool blend = !config_is_ntsc();
  if (blend) {
    ensure_blend_tables();
  }
  int start_odd = (s_frame_no & 1);
  static const uint8_t kPad[3] = {0, 0, 0};
  bool ok = true;
  for (int y = Screen_HEIGHT - 1; y >= 0; y--) {
    int odd_line = start_odd ^ (y & 1);
    uint8_t row[Screen_WIDTH * kBpp];
    memset(row, 0, (size_t)kVisibleX1 * kBpp);
    memset(row + (size_t)kVisibleX2 * kBpp, 0,
           (size_t)(Screen_WIDTH - kVisibleX2) * kBpp);
    for (int x = kVisibleX1; x < kVisibleX2; x++) {
      uint32_t rgb = monitor_transform(
          display_pixel(src, y, x, blend, odd_line));
      row[x * 3] = (uint8_t)(rgb & 0xff);
      row[x * 3 + 1] = (uint8_t)((rgb >> 8) & 0xff);
      row[x * 3 + 2] = (uint8_t)((rgb >> 16) & 0xff);
    }
    if (fwrite(row, 1, sizeof(row), fp) != sizeof(row)) {
      ok = false;
      break;
    }
    if (row_pad != 0 &&
        fwrite(kPad, 1, (size_t)row_pad, fp) != (size_t)row_pad) {
      ok = false;
      break;
    }
  }
  fclose(fp);
  return ok;
}

void video_refresh_palette(void) {
  // Rebuild the blend tables and re-render so slider/monitor changes
  // preview live while the menu (and the frame loop) is paused. The
  // present pushes the pixels out; the UI layer stays on top.
  bool blend = !config_is_ntsc();
  if (blend) {
    ensure_blend_tables();
    // One-shot build/path marker (pitch 768 = RGB565 layer).
    static bool s_logged = false;
    if (!s_logged) {
      s_logged = true;
      debug_log("Atari800 video: blend=1 pitch=%d", s_fb_pitch);
    }
  }
  render_frame_buffer();
  if (s_fb != nullptr) {
    circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 0);
  }
}

void video_init(void) {
  uint8_t *pixels = nullptr;
  int pitch = 0;

  // Direct-color RGB565: PAL blending and monitor mapping produce per-pixel
  // RGB that cannot live in a 256-entry indexed palette.
  circle_alloc_fbl(FB_LAYER_VIC, 1, &pixels, Screen_WIDTH, Screen_HEIGHT,
                   &pitch);
  debug_log("Atari800 video_init: fbl pixels=%p pitch=%d (%dx%d)",
            (const void *)pixels, pitch, Screen_WIDTH, Screen_HEIGHT);
  if (pixels == nullptr || pitch == 0) {
    debug_log("Atari800 video_init FAILED");
    return;
  }
  // Publish the VIC geometry so the menu/UI layer sizes itself
  // (ui_output_geometry_changed). VICE does this from draw_buffer_alloc;
  // without it the menu renders nowhere. Our own VIC geometry is left
  // untouched (unlike emux_frame_buffer_changed, which would program
  // VICE canvas state we don't have).
  {
    int dpx = 0, dpy = 0, fbw = 0, fbh = 0, sw = 0, sh = 0, dw = 0, dh = 0;
    circle_get_fbl_dimensions(FB_LAYER_VIC, &dpx, &dpy, &fbw, &fbh, &sw, &sh,
                              &dw, &dh);
    debug_log("Atari800 video: display=%dx%d", dpx, dpy);
  }
  emux_geometry_changed(FB_LAYER_VIC);

  // Describe our canvas to the shared video-settings machinery (integer
  // scaling arranges, border trims, "apply at boot"). VICE fills this
  // from its per-machine videoarch; without it all border math divides
  // against zeros and clamps to degenerate modes ("fbw too large",
  // narrow pictures). Full visible Atari frame with side borders.
  canvas_state[VIC_INDEX].fb_width = Screen_WIDTH;
  canvas_state[VIC_INDEX].fb_height = Screen_HEIGHT;
  canvas_state[VIC_INDEX].gfx_w = 336;
  canvas_state[VIC_INDEX].gfx_h = 240;
  canvas_state[VIC_INDEX].max_border_w = (Screen_WIDTH - 336) / 2;
  canvas_state[VIC_INDEX].max_border_h = 0;
  canvas_state[VIC_INDEX].max_padding_w = 0;
  canvas_state[VIC_INDEX].max_padding_h = 0;
  canvas_state[VIC_INDEX].border_w = (Screen_WIDTH - 336) / 2;
  canvas_state[VIC_INDEX].border_h = 0;

  s_fb = pixels;
  s_fb_pitch = pitch;
  s_blend_valid = false;
  s_frame_no = 0;

  circle_clear_fbl(FB_LAYER_VIC);
  circle_show_fbl(FB_LAYER_VIC);
  circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 1);
}

static unsigned long s_present_count = 0;
static unsigned long s_ensure_us = 0;
static unsigned long s_present_us = 0;

void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us) {
  if (presents) *presents = s_present_count;
  if (ensure_us) {
    *ensure_us = s_ensure_us;
    s_ensure_us = 0;
  }
  if (present_us) {
    *present_us = s_present_us;
    s_present_us = 0;
  }
}

void video_render(void) {
  if (s_fb == nullptr) {
    return;
  }

  // VICE calls this every frame from videoarch; it re-shows layers hidden
  // by video adjustments (notably the VIC layer, hidden while new stretch
  // applies) and maintains STATUS overlays. Without it any video-menu
  // change blacks the VIC layer until reboot.
  unsigned long te0 = CTimer::GetClockTicks();
  emux_ensure_video();
  s_ensure_us += CTimer::GetClockTicks() - te0;

  render_frame_buffer();

  // Present without vblank wait, matching VICE's normal emulation path
  // (present_sync=0; sync only with UI/status overlays). Waiting every
  // frame cost a full display period and halved the frame rate.
  unsigned long tp0 = CTimer::GetClockTicks();
  circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 0);
  s_present_us += CTimer::GetClockTicks() - tp0;
  s_present_count++;
  s_frame_no++;
}

}  // namespace atari800
}  // namespace bmx
