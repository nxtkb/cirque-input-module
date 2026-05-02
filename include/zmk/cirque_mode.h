#pragma once

#include <stdbool.h>
#include <zephyr/device.h>

enum zmk_cirque_mode_action {
	ZMK_CIRQUE_MODE_ACTION_RELATIVE = 0,
	ZMK_CIRQUE_MODE_ACTION_ABSOLUTE = 1,
	ZMK_CIRQUE_MODE_ACTION_TOGGLE = 2,
};

int zmk_cirque_mode_apply(const struct device *dev, enum zmk_cirque_mode_action action);
bool zmk_cirque_mode_is_relative(const struct device *dev);
bool zmk_cirque_mode_get_relative(void);
bool zmk_cirque_mode_is_known(void);
