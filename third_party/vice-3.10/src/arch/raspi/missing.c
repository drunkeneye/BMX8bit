/*
 * missing.c
 *
 * Written by
 *  Randy Rossi <randy.rossi@gmail.com>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"
#include "missing.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>

#include "ui.h"
#include "arch/shared/uiactions.h"
#include "arch/shared/coproc.h"
#include "lib/libzmbv/zmbv.h"
#include "lib/libzmbv/zmbv_avi.h"
#include "raspi_machine.h"
#include "resources.h"
#include "signals.h"
#include "uiapi.h"
#include "vicesocket.h"
#include "vsync.h"

#define BMC_WEAK __attribute__((weak))

static int legacy_warp_mode;
static int legacy_datasette = 1;
static int legacy_drive_true_emulation = 1;
static int legacy_virtual_devices;
static int legacy_iec_device[4];
static int legacy_vicii_hw_scale;
static int legacy_vic_hw_scale;
static int legacy_vdc_hw_scale;
static int legacy_ted_hw_scale;
static int legacy_crtc_hw_scale;

static int set_legacy_int(int val, void *param)
{
  *(int *)param = val;
  return 0;
}

static int set_legacy_warp_mode(int val, void *param)
{
  legacy_warp_mode = val ? 1 : 0;
  vsync_set_warp_mode(legacy_warp_mode);
  return 0;
}

static int set_legacy_iec_device(int val, void *param)
{
  const unsigned int unit = vice_ptr_to_uint(param);
  char resource_name[16];

  if (unit < 8 || unit > 11) {
    return -1;
  }

  legacy_iec_device[unit - 8] = val ? 1 : 0;
  snprintf(resource_name, sizeof(resource_name), "BusDevice%u", unit);

  /*
   * The compatibility resources are registered before the serial bus
   * resources.  Their factory values therefore cannot be forwarded yet,
   * while values loaded later from an old config file can and should be.
   */
  if (resources_exists(resource_name)) {
    return resources_set_int(resource_name, legacy_iec_device[unit - 8]);
  }
  return 0;
}

static const resource_int_t bmc64_compat_resources_int[] = {
    { "WarpMode", 0, RES_EVENT_STRICT, (resource_value_t)0,
      &legacy_warp_mode, set_legacy_warp_mode, NULL },
    { "Datasette", 1, RES_EVENT_NO, NULL,
      &legacy_datasette, set_legacy_int, &legacy_datasette },
    { "DriveTrueEmulation", 1, RES_EVENT_NO, NULL,
      &legacy_drive_true_emulation, set_legacy_int, &legacy_drive_true_emulation },
    { "VirtualDevices", 0, RES_EVENT_NO, NULL,
      &legacy_virtual_devices, set_legacy_int, &legacy_virtual_devices },
    { "IECDevice8", 0, RES_EVENT_NO, NULL,
      &legacy_iec_device[0], set_legacy_iec_device, (void *)8 },
    { "IECDevice9", 0, RES_EVENT_NO, NULL,
      &legacy_iec_device[1], set_legacy_iec_device, (void *)9 },
    { "IECDevice10", 0, RES_EVENT_NO, NULL,
      &legacy_iec_device[2], set_legacy_iec_device, (void *)10 },
    { "IECDevice11", 0, RES_EVENT_NO, NULL,
      &legacy_iec_device[3], set_legacy_iec_device, (void *)11 },
    { "VICIIHwScale", 0, RES_EVENT_NO, NULL,
      &legacy_vicii_hw_scale, set_legacy_int, &legacy_vicii_hw_scale },
    { "VICHwScale", 0, RES_EVENT_NO, NULL,
      &legacy_vic_hw_scale, set_legacy_int, &legacy_vic_hw_scale },
    { "VDCHwScale", 0, RES_EVENT_NO, NULL,
      &legacy_vdc_hw_scale, set_legacy_int, &legacy_vdc_hw_scale },
    { "TEDHwScale", 0, RES_EVENT_NO, NULL,
      &legacy_ted_hw_scale, set_legacy_int, &legacy_ted_hw_scale },
    { "CrtcHwScale", 0, RES_EVENT_NO, NULL,
      &legacy_crtc_hw_scale, set_legacy_int, &legacy_crtc_hw_scale },
    RESOURCE_INT_LIST_END
};

// ------------------------------------------------------------------------
// These are stubs to get things compiling. The vast majority of these
// routines will not require an implementation. Once a routine is given
// an implementation, it should be moved out of missing.c and into
// one of the other .c files in this directory (or a new one if it deserves
// it).
// ------------------------------------------------------------------------

char *ui_get_file(const char *format, ...) { return 0; }
char *uimon_get_in(char **p1, const char *p2) { return 0; }
char video_canvas_can_resize(struct video_canvas_s *canvas) { return 0; }
int archdep_rtc_get_centisecond(void) { return 0; }
int c128ui_init_early(void) { return 0; }
int c128ui_init(void) {
  ui_init_menu();
  return 0;
}
int c64dtvui_init_early(void) { return 0; }
int c64dtvui_init(void) { return 0; }
int c64scui_init_early(void) { return 0; }
int c64scui_init(void) {
  ui_init_menu();
  return 0;
}
int c64ui_init_early(void) { return 0; }
int c64ui_init(void) {
  ui_init_menu();
  return 0;
}
int cbm2ui_init_early(void) { return 0; }
int cbm2ui_init(void) { return 0; }
int cbm5x0ui_init_early(void) { return 0; }
int cbm5x0ui_init(void) { return 0; }
int console_close_all(void) { return 0; }
int console_init(void) { return 0; }
int dthread_ui_init_finish(void) { return 0; }
int dthread_ui_init(int *argc, char **argv) { return 0; }
int joy_arch_cmdline_options_init(void) { return 0; }
int joy_arch_resources_init(void) { return 0; }
int joy_arch_set_device(int port_idx, int new_dev) { return 0; }
int mui_init(void) { return 0; }
int petui_init_early(void) { return 0; }
int petui_init(void) {
  ui_init_menu();
  return 0;
}
int plus4ui_init_early(void) { return 0; }
int plus4ui_init(void) {
  ui_init_menu();
  return 0;
}
int scpu64ui_init_early(void) { return 0; }
int scpu64ui_init(void) {
  ui_init_menu();
  return 0;
}
int ui_cmdline_options_init(void) { return 0; }
int ui_extend_image_dialog(void) { return 0; }
int ui_hotkeys_cmdline_options_init(void) { return 0; }
int ui_hotkeys_resources_init(void) { return 0; }
int ui_init2(int *argc, char **argv) { return 0; }
int ui_init_finalize(void) { return 0; }
int ui_init_finish2(void) { return 0; }
int ui_init_finish(void) { return 0; }
int ui_init(void) { return 0; }
void ui_dispatch_events(void) {}
void ui_display_statustext(const char *text, bool fadeout) {}
int uimon_petscii_out(const char *buffer, int len) { return 0; }
int uimon_petscii_upper_out(const char *buffer, int len) { return 0; }
int uimon_scrcode_out(const char *buffer, int len) { return 0; }
int uimon_scrcode_upper_out(const char *buffer, int len) { return 0; }
int uimon_out(const char *buffer) { return 0; }
int ui_resources_init(void) { return resources_register_int(bmc64_compat_resources_int); }
int vic20ui_init_early(void) { return 0; }
int vic20ui_init(void) {
  ui_init_menu();
  return 0;
}
int video_arch_cmdline_options_init(void) { return 0; }
int video_arch_resources_init(void) { return 0; }
int video_canvas_refresh_dx9(video_canvas_t *canvas, unsigned int xs,
                             unsigned int ys, unsigned int xi, unsigned int yi,
                             unsigned int w, unsigned int h) {
  return 0;
}
int video_init(void) { return 0; }
int vsid_ui_init(void) { return 0; }
struct console_s *uimon_window_open(void) {
  return 0;
}
struct console_s *uimon_window_resume(void) {
  return 0;
}
ui_jam_action_t ui_jam_dialog(const char *format, ...) { return UI_JAM_NONE; }
video_canvas_t *video_canvas_create_ddraw(video_canvas_t *canvas) { return 0; }
video_canvas_t *video_canvas_create_dx9(video_canvas_t *canvas,
                                        unsigned int *width,
                                        unsigned int *height) {
  return 0;
}
void archdep_signals_init(int do_core_dumps) {}
void signals_pipe_set(void) {}
void signals_pipe_unset(void) {}
void c128ui_shutdown(void) {}
void c64dtvui_shutdown(void) {}
void c64scui_shutdown(void) {}
void c64ui_shutdown(void) {}
void cbm2ui_shutdown(void) {}
void cbm5x0ui_shutdown(void) {}
void fullscreen_capability(struct cap_fullscreen_s *cap_fullscreen) {}
void arch_ui_activate(void) {}
void joystick_arch_init(void) {}
void joystick_arch_shutdown(void) {}
void joy_arch_init_default_mapping(int joynum) {}
BMC_WEAK void kbd_arch_init(void) {}
void kbd_arch_shutdown(void) {}
BMC_WEAK void kbd_initialize_numpad_joykeys(int *joykeys) {}
BMC_WEAK signed long kbd_arch_keyname_to_keynum(char *keyname) { return -1; }
BMC_WEAK const char *kbd_arch_keynum_to_keyname(signed long keynum) {
  static char keyname[20];

  memset(keyname, 0, sizeof(keyname));
  snprintf(keyname, sizeof(keyname), "%ld", keynum);
  return keyname;
}
void petui_shutdown(void) {}
void plus4ui_shutdown(void) {}
void scpu64ui_shutdown(void) {}
void sdl_ui_init_draw_params(void) {}
void sdl_ui_init_finalize(void) {}
void signals_init(int do_core_dumps) {}
void tui_error(const char *format, ...) {}
void tui_init(void) {}
void ui_display_drive_current_image(unsigned int unit_number,
                                    unsigned int drive_number,
                                    const char *image) {}
void ui_display_drive_track(unsigned int drive_number, unsigned int drive_base,
                            unsigned int half_track_number,
                            unsigned int disk_side) {}
void ui_display_event_time(unsigned int current, unsigned int total) {}
void ui_display_joyport(uint16_t *joyport) {}
void ui_display_playback(int playback_status, char *version) {}
void ui_display_recording(int recording_status) {}
void ui_display_reset(int device, int mode) {}
void ui_display_tape_current_image(int port, const char *image) {}
void ui_error_string(const char *text) {}
BMC_WEAK void ui_message(const char *format, ...) {}
BMC_WEAK int ui_pause_active(void) { return 0; }
BMC_WEAK void ui_pause_enable(void) {}
BMC_WEAK int ui_pause_loop_iteration(void) { return 0; }
BMC_WEAK void ui_pause_disable(void) {}
void ui_init_with_args(int *argc, char **argv) {}
void ui_init_checkbox_style(void) {}
void ui_init_drive_status_widget(void) {}
void ui_init_joystick_status_widget(void) {}
void uimon_notify_change(void) {}
void uimon_set_interface(struct monitor_interface_s **p1, int p2) {}
void uimon_window_close(void) {}
void uimon_window_suspend(void) {}
void ui_resources_shutdown(void) {}
void ui_set_tape_status(int port, int tape_status) {}
void ui_shutdown(void) {}
void ui_update_menus(void) {}
void vic20ui_shutdown(void) {}
void video_arch_resources_shutdown(void) {}
void video_canvas_destroy_ddraw(video_canvas_t *canvas) {}
void video_canvas_destroy(struct video_canvas_s *canvas) {}
void video_canvas_refresh_ddraw(video_canvas_t *canvas, unsigned int xs,
                                unsigned int ys, unsigned int xi,
                                unsigned int yi, unsigned int w,
                                unsigned int h) {}
void video_canvas_resize(struct video_canvas_s *canvas, char resize_canvas) {}
void video_canvas_set_palette_ddraw_8bit(video_canvas_t *canvas) {}
void video_shutdown_dx9(void) {}
void video_shutdown(void) {}
void vsyncarch_display_speed(double speed, double fps, int warp_enabled) {}
void ui_display_volume(int vol) {}
void ui_actions_init(void) {}
void ui_actions_shutdown(void) {}
void ui_actions_set_dispatch(void (*dispatch)(ui_action_map_t *)) {}
void ui_actions_register(const ui_action_map_t *mappings) {}
const char *ui_action_get_name(int action) { return ""; }
const char *ui_action_get_desc(int action) { return ""; }
int ui_action_get_id(const char *name) { return ACTION_NONE; }
bool ui_action_is_valid(int action) { return action > ACTION_NONE && action < ACTION_ID_COUNT; }
ui_action_info_t *ui_action_get_info_list(void) { return NULL; }
void ui_action_trigger(int action) {}
void ui_action_finish(int action) {}
ui_action_map_t *ui_action_map_get(int action) { return NULL; }
ui_action_map_t *ui_action_map_get_by_hotkey(uint32_t vice_keysym, uint32_t vice_modmask) { return NULL; }
ui_action_map_t *ui_action_map_get_by_arch_hotkey(uint32_t arch_keysym, uint32_t arch_modmask) { return NULL; }
void ui_action_map_clear_hotkey(ui_action_map_t *map) {}
void ui_action_map_clear_hotkey_by_action(int action) {}
void ui_action_map_clear_hotkey_by_hotkey(uint32_t vice_keysym, uint32_t vice_modmask) {}
ui_action_map_t *ui_action_map_set_hotkey(int action, uint32_t vice_keysym, uint32_t vice_modmask, uint32_t arch_keysym, uint32_t arch_modmask) { return NULL; }
void ui_action_map_set_hotkey_by_map(ui_action_map_t *map, uint32_t vice_keysym, uint32_t vice_modmask, uint32_t arch_keysym, uint32_t arch_modmask) {}
int fork_coproc(int *fd_wr, int *fd_rd, char *cmd, vice_pid_t *childpid) { return -1; }
void kill_coproc(vice_pid_t pid) {}
#ifndef HAVE_NETWORK
vice_network_socket_t *vice_network_client(const vice_network_socket_address_t *server_address) { return NULL; }
vice_network_socket_address_t *vice_network_address_generate(const char *address, unsigned short port) { return NULL; }
int vice_network_socket_close(vice_network_socket_t *sockfd) { return 0; }
ssize_t vice_network_send(vice_network_socket_t *sockfd, const void *buffer, size_t buffer_length, int flags) { return -1; }
#endif
zmbv_format_t zmbv_bpp_to_format(int bpp) { return ZMBV_FORMAT_NONE; }
int zmbv_work_buffer_size(int width, int height, zmbv_format_t fmt) { return -1; }
zmbv_codec_t zmbv_codec_new(zmvb_init_flags_t flags, int complevel) { return NULL; }
void zmbv_codec_free(zmbv_codec_t zc) {}
int zmbv_encode_setup(zmbv_codec_t zc, int width, int height) { return -1; }
int zmbv_encode_prepare_frame(zmbv_codec_t zc, zmvb_prepare_flags_t flags, zmbv_format_t fmt, const void *pal, void *outbuf, int outbuf_size) { return -1; }
int zmbv_encode_lines(zmbv_codec_t zc, int line_count, const void *const line_ptrs[]) { return -1; }
int zmvb_encode_finish_frame(zmbv_codec_t zc) { return -1; }
int zmbv_get_width(zmbv_codec_t zc) { return -1; }
int zmbv_get_height(zmbv_codec_t zc) { return -1; }
zmbv_avi_t zmbv_avi_start(const char *fname, int width, int height, double fps, int audiorate) { return NULL; }
int zmbv_avi_stop(zmbv_avi_t zavi) { return -1; }
int zmbv_avi_write_chunk(zmbv_avi_t zavi, const char tag[4], uint32_t size, const void *data, uint32_t flags) { return -1; }
int zmbv_avi_write_chunk_video(zmbv_avi_t zavi, const void *framedata, int size) { return -1; }
int zmbv_avi_write_chunk_audio(zmbv_avi_t zavi, const void *data, int size) { return -1; }
char *ui_action_map_get_hotkey_label(ui_action_map_t *map) { return NULL; }
char *ui_action_get_hotkey_label(int action) { return NULL; }
int ui_action_id_fliplist_add(int unit, int drive) { return ACTION_NONE; }
int ui_action_id_fliplist_remove(int unit, int drive) { return ACTION_NONE; }
int ui_action_id_fliplist_next(int unit, int drive) { return ACTION_NONE; }
int ui_action_id_fliplist_previous(int unit, int drive) { return ACTION_NONE; }
int ui_action_id_fliplist_clear(int unit, int drive) { return ACTION_NONE; }
int ui_action_id_fliplist_load(int unit, int drive) { return ACTION_NONE; }
int ui_action_id_fliplist_save(int unit, int drive) { return ACTION_NONE; }
int ui_action_id_drive_attach(int unit, int drive) { return ACTION_NONE; }
int ui_action_id_drive_detach(int unit, int drive) { return ACTION_NONE; }
int ui_action_id_drive_reset(int unit) { return ACTION_NONE; }
int ui_action_id_drive_reset_config(int unit) { return ACTION_NONE; }
int ui_action_id_drive_reset_install(int unit) { return ACTION_NONE; }
bool ui_action_def(int action, const char *hotkey) { return false; }
bool ui_action_redef(int action, const char *hotkey) { return false; }
bool ui_action_undef(int action) { return false; }
void ui_hotkeys_init(const char *prefix) {}
void ui_hotkeys_shutdown(void) {}
void ui_hotkeys_remove_all(void) {}
void ui_hotkeys_install_by_map(ui_action_map_t *map) {}
void ui_hotkeys_update_by_map(ui_action_map_t *map, uint32_t vice_keysym, uint32_t vice_modmask) {}
void ui_hotkeys_update_by_action(int action, uint32_t vice_keysym, uint32_t vice_modmask) {}
void ui_hotkeys_remove_by_map(ui_action_map_t *map) {}
void ui_hotkeys_remove_by_action(int action) {}
void ui_hotkeys_set_default_requested(bool requested) {}
bool ui_hotkeys_load(const char *path) { return false; }
void ui_hotkeys_load_vice_default(void) {}
bool ui_hotkeys_load_user_default(void) { return false; }
void ui_hotkeys_reload(void) {}
bool ui_hotkeys_save(void) { return false; }
bool ui_hotkeys_save_as(const char *path) { return false; }
const char *ui_hotkeys_vhk_filename_vice(void) { return "hotkeys.vhk"; }
char *ui_hotkeys_vhk_filename_user(void) { return NULL; }
char *ui_hotkeys_vhk_full_path_user(void) { return NULL; }
char *ui_hotkeys_vhk_full_path_vice(void) { return NULL; }
char *ui_hotkeys_vhk_source_path(void) { return NULL; }
uint32_t ui_hotkeys_arch_keysym_from_arch(uint32_t arch_keysym) { return arch_keysym; }
uint32_t ui_hotkeys_arch_keysym_to_arch(uint32_t vice_keysym) { return vice_keysym; }
uint32_t ui_hotkeys_arch_modifier_from_arch(uint32_t arch_mod) { return arch_mod; }
uint32_t ui_hotkeys_arch_modifier_to_arch(uint32_t vice_mod) { return vice_mod; }
uint32_t ui_hotkeys_arch_modmask_from_arch(uint32_t arch_modmask) { return arch_modmask; }
uint32_t ui_hotkeys_arch_modmask_to_arch(uint32_t vice_modmask) { return vice_modmask; }
void ui_hotkeys_arch_init(void) {}
void ui_hotkeys_arch_shutdown(void) {}
void ui_hotkeys_arch_install_by_map(ui_action_map_t *map) {}
void ui_hotkeys_arch_update_by_map(ui_action_map_t *map, uint32_t vice_keysym, uint32_t vice_modmask) {}
void ui_hotkeys_arch_remove_by_map(ui_action_map_t *map) {}
void main_exit(void) {}
