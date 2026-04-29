/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_pointing_speed

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/pointing_speed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static uint8_t pointer_speed_index = 1;
static uint8_t scroll_speed_index = 1;

static uint8_t *speed_index_for_target(enum zmk_pointing_speed_target target)
{
	switch (target) {
	case ZMK_POINTING_SPEED_TARGET_SCROLL:
		return &scroll_speed_index;
	case ZMK_POINTING_SPEED_TARGET_POINTER:
	default:
		return &pointer_speed_index;
	}
}

uint8_t zmk_pointing_speed_get_index(enum zmk_pointing_speed_target target)
{
	return *speed_index_for_target(target);
}

void zmk_pointing_speed_set_initial_index(enum zmk_pointing_speed_target target, uint8_t index)
{
	*speed_index_for_target(target) = index;
}

void zmk_pointing_speed_adjust(enum zmk_pointing_speed_target target,
			       enum zmk_pointing_speed_action action)
{
	uint8_t *index = speed_index_for_target(target);

	if (action == ZMK_POINTING_SPEED_ACTION_PREV) {
		(*index)--;
	} else {
		(*index)++;
	}
}

static int on_pointing_speed_binding_pressed(struct zmk_behavior_binding *binding,
					     struct zmk_behavior_binding_event event)
{
	zmk_pointing_speed_adjust(binding->param1, binding->param2);
	return ZMK_BEHAVIOR_OPAQUE;
}

static int on_pointing_speed_binding_released(struct zmk_behavior_binding *binding,
					      struct zmk_behavior_binding_event event)
{
	return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_pointing_speed_driver_api = {
	.binding_pressed = on_pointing_speed_binding_pressed,
	.binding_released = on_pointing_speed_binding_released,
	.locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
	.get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int behavior_pointing_speed_init(const struct device *dev) { return 0; }

#define POINTING_SPEED_INST(n)                                                                      \
	BEHAVIOR_DT_INST_DEFINE(n, behavior_pointing_speed_init, NULL, NULL, NULL, POST_KERNEL,     \
				CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                    \
				&behavior_pointing_speed_driver_api);

DT_INST_FOREACH_STATUS_OKAY(POINTING_SPEED_INST)
