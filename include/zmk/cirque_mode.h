#pragma once

#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

enum zmk_cirque_mode_action {
	ZMK_CIRQUE_MODE_ACTION_RELATIVE = 0,
	ZMK_CIRQUE_MODE_ACTION_ABSOLUTE = 1,
	ZMK_CIRQUE_MODE_ACTION_TOGGLE = 2,
};

#define ZMK_CIRQUE_MODE_STATUS_INPUT_TYPE INPUT_EV_MSC
#define ZMK_CIRQUE_MODE_STATUS_INPUT_CODE 0x4351
#define ZMK_CIRQUE_MODE_STATUS_INPUT_VALUE_ABSOLUTE 0
#define ZMK_CIRQUE_MODE_STATUS_INPUT_VALUE_RELATIVE 1

int zmk_cirque_mode_apply(const struct device *dev, enum zmk_cirque_mode_action action);
bool zmk_cirque_mode_is_relative(const struct device *dev);
int zmk_cirque_mode_report(const struct device *dev, bool relative_mode);
bool zmk_cirque_mode_get_relative(void);
bool zmk_cirque_mode_is_known(void);
