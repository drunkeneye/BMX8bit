//
// atari800_input.cpp
//
// Atari800 input (keyboard/joystick/mouse) for BMX bare-metal.
//
// BMX routes USB keyboard/mouse/gamepad through the standard emu_* C API into
// ring buffers (pending_emu_key / pending_emu_joy / mouse). This module drains
// those buffers and turns them into the single-key input_template_t that
// libatari800 consumes.
//

#include "atari800_input.h"
#include "atari800_bridge.h"
#include "atari800_debug.h"

#include "defs.h"

extern "C" {
#include "third_party/atari800/src/config.h"
#include "third_party/atari800/src/akey.h"
#include "third_party/atari800/src/input.h"
#include "third_party/common/circle.h"
#include "third_party/common/emux_api.h"
#include "third_party/common/keycodes.h"
}

#include <string.h>

// BMX menu request flag (ui.c, C linkage): set to open the menu loop.
extern int ui_toggle_pending;

namespace bmx {
namespace atari800 {

// Current single-key Atari keycode, with shift/control bits already applied
// where the key itself does not encode them. Note AKEY_l == 0x00, so a zero
// keycode is VALID; AKEY_NONE (-1) means "no key".
static int s_keycode = AKEY_NONE;
// ASCII char for the keychar path (preferred: libatari800 translates it,
// which also fixes zero-valued codes like AKEY_l). 0 = none.
static int s_keychar = 0;
// True while the raw (unshifted) key is held, so modifiers can be re-applied.
static int s_last_hid = 0;
static int s_shift_down = 0;
static int s_ctrl_down = 0;
static int s_shift_explicit = 0;
static int s_ctrl_explicit = 0;
static int s_caps_toggle_pending = 0;
// Warm-start pulse requested by F5 (consumed as one input->special).
static int s_reset_pending = 0;
// Help held via a user-mapped F6..F11 (injects AKEY_HELP in the fill step).
static int s_help_held = 0;
// Joystick / console state (see s_joy_latch below for stick/fire latches)
static int s_option = 0;
static int s_select = 0;
static int s_start = 0;
// Mouse deltas accumulate on core 0 (USB) and are consumed per-frame on
// core 1. Buttons are level state (bit0=left, bit1=right, bit2=middle).
static int s_mouse_dx = 0;
static int s_mouse_dy = 0;
static int s_mouse_buttons = 0;

// Map a USB HID usage to a base Atari keycode (before shift/control are
// applied). Returns AKEY_NONE if the key has no Atari equivalent.
static int hid_to_akey(long hid) {
  switch (hid) {
  case KEYCODE_a: return AKEY_a;
  case KEYCODE_b: return AKEY_b;
  case KEYCODE_c: return AKEY_c;
  case KEYCODE_d: return AKEY_d;
  case KEYCODE_e: return AKEY_e;
  case KEYCODE_f: return AKEY_f;
  case KEYCODE_g: return AKEY_g;
  case KEYCODE_h: return AKEY_h;
  case KEYCODE_i: return AKEY_i;
  case KEYCODE_j: return AKEY_j;
  case KEYCODE_k: return AKEY_k;
  case KEYCODE_l: return AKEY_l;
  case KEYCODE_m: return AKEY_m;
  case KEYCODE_n: return AKEY_n;
  case KEYCODE_o: return AKEY_o;
  case KEYCODE_p: return AKEY_p;
  case KEYCODE_q: return AKEY_q;
  case KEYCODE_r: return AKEY_r;
  case KEYCODE_s: return AKEY_s;
  case KEYCODE_t: return AKEY_t;
  case KEYCODE_u: return AKEY_u;
  case KEYCODE_v: return AKEY_v;
  case KEYCODE_w: return AKEY_w;
  case KEYCODE_x: return AKEY_x;
  case KEYCODE_y: return AKEY_y;
  case KEYCODE_z: return AKEY_z;
  case KEYCODE_1: return AKEY_1;
  case KEYCODE_2: return AKEY_2;
  case KEYCODE_3: return AKEY_3;
  case KEYCODE_4: return AKEY_4;
  case KEYCODE_5: return AKEY_5;
  case KEYCODE_6: return AKEY_6;
  case KEYCODE_7: return AKEY_7;
  case KEYCODE_8: return AKEY_8;
  case KEYCODE_9: return AKEY_9;
  case KEYCODE_0: return AKEY_0;
  case KEYCODE_Return: return AKEY_RETURN;
  case KEYCODE_Escape: return AKEY_ESCAPE;
  case KEYCODE_Backspace: return AKEY_BACKSPACE;
  case KEYCODE_Tab: return AKEY_TAB;
  case KEYCODE_Space: return AKEY_SPACE;
  case KEYCODE_Dash: return AKEY_MINUS;
  case KEYCODE_Equals: return AKEY_EQUAL;
  case KEYCODE_LeftBracket: return AKEY_BRACKETLEFT;
  case KEYCODE_RightBracket: return AKEY_BRACKETRIGHT;
  case KEYCODE_BackSlash: return AKEY_BACKSLASH;
  case KEYCODE_SemiColon: return AKEY_SEMICOLON;
  case KEYCODE_SingleQuote: return AKEY_QUOTE;
  case KEYCODE_Comma: return AKEY_COMMA;
  case KEYCODE_Period: return AKEY_FULLSTOP;
  case KEYCODE_Slash: return AKEY_SLASH;
  // Cursor arrows use the hardware 6-bit KBCODE values, NOT AKEY_UP etc:
  // upstream ORs 0x80 into those, but on real hardware bit 7 is the CTRL
  // flag (AKEY_CTRL), so plain arrows are $0E/$0F/$06/$07. Raw-kbcode
  // readers (e.g. MAFIA's flag menu comparing against 14/15) fail with
  // $8E. Shift/Ctrl still OR correctly on top via the modifier path.
  case KEYCODE_Up: return 0x0E;
  case KEYCODE_Down: return 0x0F;
  case KEYCODE_Left: return 0x06;
  case KEYCODE_Right: return 0x07;
  case KEYCODE_F1: return AKEY_HELP;
  default: return AKEY_NONE;
  }
}

// Map a USB HID usage to its unshifted US-layout ASCII char for the
// keychar path (preferred: libatari800 translates it, which also handles
// zero-valued codes like AKEY_l that the raw keycode field cannot carry).
// Shifted symbols are resolved by the caller via s_shift_down. Returns 0
// for keys without an ASCII form (arrows, F-keys, ...).
static int hid_to_ascii(long hid, int shifted) {
  if (hid >= KEYCODE_a && hid <= KEYCODE_z) {
    return (int)'a' + (int)(hid - KEYCODE_a);
  }
  if (!shifted) {
    switch (hid) {
    case KEYCODE_1: return '1';
    case KEYCODE_2: return '2';
    case KEYCODE_3: return '3';
    case KEYCODE_4: return '4';
    case KEYCODE_5: return '5';
    case KEYCODE_6: return '6';
    case KEYCODE_7: return '7';
    case KEYCODE_8: return '8';
    case KEYCODE_9: return '9';
    case KEYCODE_0: return '0';
    case KEYCODE_Space: return ' ';
    case KEYCODE_Return: return '\n';
    case KEYCODE_Escape: return 27;
    case KEYCODE_Backspace: return '\b';
    case KEYCODE_Tab: return '\t';
    case KEYCODE_Dash: return '-';
    case KEYCODE_Equals: return '=';
    case KEYCODE_LeftBracket: return '[';
    case KEYCODE_RightBracket: return ']';
    case KEYCODE_BackSlash: return '\\';
    case KEYCODE_SemiColon: return ';';
    case KEYCODE_SingleQuote: return '\'';
    case KEYCODE_Comma: return ',';
    case KEYCODE_Period: return '.';
    case KEYCODE_Slash: return '/';
    case KEYCODE_BackQuote: return '`';
    case KEYCODE_Delete: return 127;
    default: return 0;
    }
  }
  switch (hid) {
  case KEYCODE_1: return '!';
  case KEYCODE_2: return '@';
  case KEYCODE_3: return '#';
  case KEYCODE_4: return '$';
  case KEYCODE_5: return '%';
  case KEYCODE_6: return '^';
  case KEYCODE_7: return '&';
  case KEYCODE_8: return '*';
  case KEYCODE_9: return '(';
  case KEYCODE_0: return ')';
  case KEYCODE_Dash: return '_';
  case KEYCODE_Equals: return '+';
  case KEYCODE_LeftBracket: return '{';
  case KEYCODE_RightBracket: return '}';
  case KEYCODE_BackSlash: return '|';
  case KEYCODE_SemiColon: return ':';
  case KEYCODE_SingleQuote: return '"';
  case KEYCODE_Comma: return '<';
  case KEYCODE_Period: return '>';
  case KEYCODE_Slash: return '?';
  case KEYCODE_BackQuote: return '~';
  default: break;
  }
  if (hid >= KEYCODE_a && hid <= KEYCODE_z) {
    return (int)'a' + (int)(hid - KEYCODE_a);
  }
  return 0;
}

// Latch bit layout (matches BMX/VICE joy queue): bit0=up, bit1=down,
// bit2=left, bit3=right, bit4=fire. Ports in pending_emu_joy are 1-based
// (joydev port = dev+1); atari ports 0/1 map from BMX ports 1/2.
static int s_joy_latch[2] = {0, 0};

static void drain_keys(void) {
  // NOTE: head/tail are monotonic counters; mask only to index. Wrapping
  // head itself (e.g. & 0xf on store) desyncs from tail after 16 events
  // and loops forever on stale entries, hanging the frame loop.
  while (pending_emu_key.head != pending_emu_key.tail) {
    int i = pending_emu_key.head & 0xf;
    long key = pending_emu_key.key[i];
    int pressed = pending_emu_key.pressed[i];
    int mod = pending_emu_key.mod[i];
    pending_emu_key.head++;
    (void)mod;

    static bool s_first_key_logged = false;
    if (!s_first_key_logged) {
      s_first_key_logged = true;
      debug_log("Atari800: first key event hid=%ld pressed=%d", key, pressed);
    }

    // Modifier state: the event mod bitmask (ViceKeyboardModifierMask:
    // bit0/1 = L/RShift, bit2/3 = L/RCtrl) is authoritative on USB paths;
    // the explicit latch covers paths that send bare shift presses (GPIO).
    if (key == KEYCODE_LeftShift || key == KEYCODE_RightShift) {
      s_shift_explicit = pressed ? 1 : 0;
    }
    if (key == KEYCODE_LeftControl || key == KEYCODE_RightControl) {
      s_ctrl_explicit = pressed ? 1 : 0;
    }
    s_shift_down = (mod & 0x03) ? 1 : s_shift_explicit;
    s_ctrl_down = (mod & 0x0c) ? 1 : s_ctrl_explicit;
    if (key == KEYCODE_LeftShift || key == KEYCODE_RightShift ||
        key == KEYCODE_LeftControl || key == KEYCODE_RightControl) {
      continue;
    }

    // Console keys via F-keys (F12 is the BMX menu):
    // F1=Help (keyboard key), F2=Start, F3=Select, F4=Option, F5=Reset.
    if (key == KEYCODE_F2) {
      s_start = pressed ? 1 : 0;
      continue;
    }
    if (key == KEYCODE_F3) {
      s_select = pressed ? 1 : 0;
      continue;
    }
    if (key == KEYCODE_F4) {
      s_option = pressed ? 1 : 0;
      continue;
    }
    if (key == KEYCODE_F5) {
      // Atari Reset = warm start. Single pulse: the core reboots on
      // every frame while special is set.
      if (pressed) {
        s_reset_pending = 1;
      }
      continue;
    }

    // User-mapped function keys F6..F11 (Keyboard menu; 0=None, 1=Help,
    // 2=Start, 3=Select, 4=Option, 5=Reset, 6=Menu). Console actions mirror
    // the fixed F1..F5 keys above; Menu raises the BMX menu request.
    if (key >= KEYCODE_F6 && key <= KEYCODE_F11) {
      int slot = (int)(key - KEYCODE_F6);
      int action = (slot >= 0 && slot < 6) ? g_fkey_action[slot] : 0;
      switch (action) {
        case 1:
          s_help_held = pressed ? 1 : 0;
          break;
        case 2:
          s_start = pressed ? 1 : 0;
          break;
        case 3:
          s_select = pressed ? 1 : 0;
          break;
        case 4:
          s_option = pressed ? 1 : 0;
          break;
        case 5:
          if (pressed) {
            s_reset_pending = 1;
          }
          break;
        case 6: {
          if (pressed) {
            ui_toggle_pending = 2;
          }
          break;
        }
        default:
          break;
      }
      continue;
    }

    // Remember the held key; translation happens per-poll below so held
    // keys track shift changes. CapsLock is one-shot (a held code would
    // toggle caps repeatedly).
    if (key == KEYCODE_CapsLock) {
      if (pressed) {
        s_caps_toggle_pending = 1;
      }
      continue;
    }
    if (pressed) {
      if (hid_to_ascii(key, 0) != 0 || hid_to_akey(key) != AKEY_NONE) {
        s_last_hid = (int)key;
      }
    } else if ((int)key == s_last_hid) {
      // Release of the currently held key clears it; other releases are
      // ignored (single-key libatari800 template).
      s_last_hid = 0;
    }
  }
}

static void drain_joy(void) {
  // Same monotonic-counter rule as drain_keys: never wrap head itself.
  while (pending_emu_joy.head != pending_emu_joy.tail) {
    int i = pending_emu_joy.head & 0x7f;
    int value = pending_emu_joy.value[i];
    int type = pending_emu_joy.type[i];
    int port = pending_emu_joy.port[i];
    pending_emu_joy.head++;

    // Queue ports are 1-based; ignore pot bits, keep low 5 bits.
    int idx = -1;
    if (port == 1)
      idx = 0;
    else if (port == 2)
      idx = 1;
    if (idx < 0) {
      continue;
    }
    int bits = value & 0x1f;
    switch (type) {
    case PENDING_EMU_JOY_TYPE_ABSOLUTE:
      s_joy_latch[idx] = bits;
      break;
    case PENDING_EMU_JOY_TYPE_AND:
      s_joy_latch[idx] &= bits;
      break;
    case PENDING_EMU_JOY_TYPE_OR:
      s_joy_latch[idx] |= bits;
      break;
    default:
      break;
    }
  }
}

void poll_input(input_template_t *input) {
  memset(input, 0, sizeof(input_template_t));

  // F5 warm-start pulse (special=2 -> AKEY_WARMSTART in libatari800).
  if (s_reset_pending) {
    s_reset_pending = 0;
    input->special = 2;
  }

  // Queue-depth guard: a flooding producer buries releases (128-entry
  // ring overwrites oldest). Log once per flood episode.
  {
    static int s_qwarned = 0;
    long pending = (long)pending_emu_joy.tail - (long)pending_emu_joy.head;
    if (pending > 32) {
      if (!s_qwarned) {
        s_qwarned = 1;
        debug_log("Atari800 input: joy queue depth=%ld (overflow risk)",
                  pending);
      }
    } else {
      s_qwarned = 0;
    }
  }

  drain_keys();
  drain_joy();

  // Translate the held key with CURRENT modifiers so held keys track
  // shift changes. keychar wins when set (libatari800 prefers it and it
  // carries zero-valued codes like AKEY_l); keycode covers the non-ASCII
  // remainder. Only AKEY_NONE means "no key".
  s_keychar = 0;
  s_keycode = AKEY_NONE;
  if (s_caps_toggle_pending) {
    s_caps_toggle_pending = 0;
    s_keycode = AKEY_CAPSLOCK;
  } else if (s_help_held) {
    // A user-mapped F6..F11 holds Help: report it like the F1 key.
    s_keycode = AKEY_HELP | (s_shift_down ? AKEY_SHFT : 0) |
                (s_ctrl_down ? AKEY_CTRL : 0);
  } else if (s_last_hid != 0) {
    int ascii = hid_to_ascii(s_last_hid, s_shift_down);
    if (ascii != 0) {
      s_keychar = ascii;
    } else {
      int akey = hid_to_akey(s_last_hid);
      if (akey != AKEY_NONE) {
        s_keycode = akey | (s_shift_down ? AKEY_SHFT : 0) |
                    (s_ctrl_down ? AKEY_CTRL : 0);
      }
    }
  }
  if (s_keychar != 0) {
    input->keychar = (UBYTE)s_keychar;
  } else if (s_keycode != AKEY_NONE) {
    input->keycode = (UBYTE)s_keycode;
  }
  input->shift = (UBYTE)(s_shift_down ? 1 : 0);
  input->control = (UBYTE)(s_ctrl_down ? 1 : 0);
  input->option = (UBYTE)s_option;
  input->select = (UBYTE)s_select;
  input->start = (UBYTE)s_start;

  // Template nibbles are active-HIGH (0 = centered, bit = pressed),
  // matching the queue convention (cf. VICE consumer); PLATFORM_STICK
  // inverts to active-low hardware. Fire trig is 1=pressed. (An earlier
  // revision XORed 0x0f here, pinning every direction as pressed.)
  input->joy0 = (UBYTE)(s_joy_latch[0] & 0x0f);
  input->trig0 = (UBYTE)((s_joy_latch[0] & 0x10) ? 1 : 0);
  input->joy1 = (UBYTE)(s_joy_latch[1] & 0x0f);
  input->trig1 = (UBYTE)((s_joy_latch[1] & 0x10) ? 1 : 0);

  // Mouse: consume accumulated deltas (clamped to int8 range expected by
  // libatari800) and report button levels.
  int dx = s_mouse_dx;
  int dy = s_mouse_dy;
  s_mouse_dx = 0;
  s_mouse_dy = 0;
  if (dx > 127) dx = 127;
  if (dx < -128) dx = -128;
  if (dy > 127) dy = 127;
  if (dy < -128) dy = -128;
  input->mousex = (UBYTE)(dx & 0xff);
  input->mousey = (UBYTE)(dy & 0xff);
  input->mouse_buttons = (UBYTE)(s_mouse_buttons & 0x07);

  // Diagnostics: what the core actually receives (change-triggered, so a
  // missing release line proves a lost release event).
  {
    static int s_first = 1;
    static UBYTE s_j0 = 0, s_j1 = 0, s_t0 = 0, s_t1 = 0;
    static int s_kc = 0, s_kch = 0;
    static UBYTE s_mod = 0;
    int kc = (s_keychar != 0) ? -s_keychar : (int)s_keycode;
    UBYTE mod =
        (UBYTE)((s_shift_down ? 1 : 0) | (s_ctrl_down ? 2 : 0) |
                (s_option ? 4 : 0) | (s_select ? 8 : 0) |
                (s_start ? 16 : 0));
    if (s_first || input->joy0 != s_j0 || input->joy1 != s_j1 ||
        input->trig0 != s_t0 || input->trig1 != s_t1 || kc != s_kc ||
        s_keychar != s_kch || mod != s_mod) {
      s_first = 0;
      s_j0 = input->joy0;
      s_j1 = input->joy1;
      s_t0 = input->trig0;
      s_t1 = input->trig1;
      s_kc = kc;
      s_kch = s_keychar;
      s_mod = mod;
      debug_log("Atari800 input: joy0=%02x joy1=%02x t0=%d t1=%d key=%d "
                "mod=%02x",
                input->joy0, input->joy1, input->trig0, input->trig1, kc,
                mod);
    }
  }
}

}  // namespace atari800
}  // namespace bmx

// VICE provides these in arch/raspi/mousedrv.c; ATARI800 builds link no VICE
// archives so the BMX mouse driver expects them here.
extern "C" {

void emu_mouse_move(int x, int y) {
  bmx::atari800::s_mouse_dx += x;
  bmx::atari800::s_mouse_dy += y;
}

void emu_mouse_button_left(int pressed) {
  if (pressed)
    bmx::atari800::s_mouse_buttons |= 0x01;
  else
    bmx::atari800::s_mouse_buttons &= ~0x01;
}

void emu_mouse_button_right(int pressed) {
  if (pressed)
    bmx::atari800::s_mouse_buttons |= 0x02;
  else
    bmx::atari800::s_mouse_buttons &= ~0x02;
}

void emu_mouse_button_middle(int pressed) {
  if (pressed)
    bmx::atari800::s_mouse_buttons |= 0x04;
  else
    bmx::atari800::s_mouse_buttons &= ~0x04;
}

void emu_mouse_wheel_up(int pressed) {
  (void)pressed;
}

void emu_mouse_wheel_down(int pressed) {
  (void)pressed;
}

}  // extern "C"
