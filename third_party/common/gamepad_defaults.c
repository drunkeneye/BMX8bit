/*
 * gamepad_defaults.c
 *
 * Automatic BMX mappings for Circle gamepads.
 */

#include "gamepad_defaults.h"

#include <string.h>

enum {
  GAMEPAD_BUTTON_BACK = 11,
  GAMEPAD_BUTTON_A = 9,
  GAMEPAD_BUTTON_START = 14,
  GAMEPAD_BUTTON_UP = 15,
  GAMEPAD_BUTTON_RIGHT = 16,
  GAMEPAD_BUTTON_DOWN = 17,
  GAMEPAD_BUTTON_LEFT = 18,
  GAMEPAD_BUTTON_PLUS = 19,
  GAMEPAD_BUTTON_MINUS = 20
};

static void gamepad_mapping_clear(USBGamepadMapping *mapping) {
  mapping->preference = USB_PREF_ANALOG;
  mapping->x_axis = 0;
  mapping->y_axis = 1;
  mapping->x_threshold_percent = 50;
  mapping->y_threshold_percent = 50;
  memset(mapping->buttons, BTN_ASSIGN_UNDEF, sizeof(mapping->buttons));
}

static void assign_button(USBGamepadMapping *mapping,
                          const USBGamepadCapabilities *capabilities,
                          int button,
                          int assignment) {
  if (button >= 0 && button < MAX_USB_BUTTONS &&
      button < capabilities->num_buttons) {
    mapping->buttons[button] = assignment;
  }
}

void gamepad_mapping_set_legacy(USBGamepadMapping *mapping) {
  gamepad_mapping_clear(mapping);
  mapping->buttons[0] = BTN_ASSIGN_FIRE;
}

void gamepad_mapping_set_automatic(
    USBGamepadMapping *mapping,
    const USBGamepadCapabilities *capabilities) {
  gamepad_mapping_clear(mapping);

  if (!capabilities->known_mapping) {
    if (capabilities->num_hats > 0) {
      mapping->preference = USB_PREF_HAT;
    }
    mapping->buttons[0] = BTN_ASSIGN_FIRE;
    return;
  }

  /* Circle guarantees semantic button and axis indices for known pads. */
  if (capabilities->num_axes < 2 && capabilities->num_hats > 0) {
    mapping->preference = USB_PREF_HAT;
  }

  assign_button(mapping, capabilities, GAMEPAD_BUTTON_A, BTN_ASSIGN_FIRE);
  assign_button(mapping, capabilities, GAMEPAD_BUTTON_UP, BTN_ASSIGN_UP);
  assign_button(mapping, capabilities, GAMEPAD_BUTTON_RIGHT, BTN_ASSIGN_RIGHT);
  assign_button(mapping, capabilities, GAMEPAD_BUTTON_DOWN, BTN_ASSIGN_DOWN);
  assign_button(mapping, capabilities, GAMEPAD_BUTTON_LEFT, BTN_ASSIGN_LEFT);

  if (capabilities->alternative_mapping) {
    assign_button(mapping, capabilities, GAMEPAD_BUTTON_PLUS, BTN_ASSIGN_MENU);
    assign_button(mapping, capabilities, GAMEPAD_BUTTON_MINUS,
                  BTN_ASSIGN_RUN_STOP_BACK);
  } else {
    assign_button(mapping, capabilities, GAMEPAD_BUTTON_START, BTN_ASSIGN_MENU);
    assign_button(mapping, capabilities, GAMEPAD_BUTTON_BACK,
                  BTN_ASSIGN_RUN_STOP_BACK);
  }
}

int gamepad_mapping_is_legacy(const USBGamepadMapping *mapping) {
  USBGamepadMapping legacy;
  gamepad_mapping_set_legacy(&legacy);
  return mapping->preference == legacy.preference &&
         mapping->x_axis == legacy.x_axis &&
         mapping->y_axis == legacy.y_axis &&
         mapping->x_threshold_percent == legacy.x_threshold_percent &&
         mapping->y_threshold_percent == legacy.y_threshold_percent &&
         memcmp(mapping->buttons, legacy.buttons,
                sizeof(mapping->buttons)) == 0;
}

USBMappingMode gamepad_mapping_resolve_mode(
    int mode_was_loaded,
    int loaded_mode,
    int mapping_was_loaded,
    const USBGamepadMapping *mapping) {
  if (mode_was_loaded) {
    return loaded_mode == USB_MAPPING_AUTOMATIC
               ? USB_MAPPING_AUTOMATIC
               : USB_MAPPING_CUSTOM;
  }

  if (!mapping_was_loaded || gamepad_mapping_is_legacy(mapping)) {
    return USB_MAPPING_AUTOMATIC;
  }

  return USB_MAPPING_CUSTOM;
}
