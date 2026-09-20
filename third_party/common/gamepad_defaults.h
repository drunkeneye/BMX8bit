/*
 * gamepad_defaults.h
 *
 * Automatic BMX mappings for Circle gamepads.
 */

#ifndef BMX_GAMEPAD_DEFAULTS_H
#define BMX_GAMEPAD_DEFAULTS_H

#include "joy.h"

typedef enum {
  USB_MAPPING_AUTOMATIC = 0,
  USB_MAPPING_CUSTOM = 1
} USBMappingMode;

typedef struct {
  int preference;
  int x_axis;
  int y_axis;
  int x_threshold_percent;
  int y_threshold_percent;
  int buttons[MAX_USB_BUTTONS];
} USBGamepadMapping;

typedef struct {
  int num_buttons;
  int num_axes;
  int num_hats;
  int known_mapping;
  int alternative_mapping;
} USBGamepadCapabilities;

void gamepad_mapping_set_legacy(USBGamepadMapping *mapping);
void gamepad_mapping_set_automatic(
    USBGamepadMapping *mapping,
    const USBGamepadCapabilities *capabilities);
int gamepad_mapping_is_legacy(const USBGamepadMapping *mapping);
USBMappingMode gamepad_mapping_resolve_mode(
    int mode_was_loaded,
    int loaded_mode,
    int mapping_was_loaded,
    const USBGamepadMapping *mapping);

#endif
