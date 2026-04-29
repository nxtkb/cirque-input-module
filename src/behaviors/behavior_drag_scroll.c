/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_drag_scroll

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/drag_scroll.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static bool drag_scroll_enabled;

void zmk_drag_scroll_set_enabled(bool enabled) { drag_scroll_enabled = enabled; }

bool zmk_drag_scroll_is_enabled(void) { return drag_scroll_enabled; }

static int on_drag_scroll_binding_pressed(struct zmk_behavior_binding *binding,
					  struct zmk_behavior_binding_event event)
{
	zmk_drag_scroll_set_enabled(true);
	return ZMK_BEHAVIOR_OPAQUE;
}

static int on_drag_scroll_binding_released(struct zmk_behavior_binding *binding,
					   struct zmk_behavior_binding_event event)
{
	zmk_drag_scroll_set_enabled(false);
	return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_drag_scroll_driver_api = {
	.binding_pressed = on_drag_scroll_binding_pressed,
	.binding_released = on_drag_scroll_binding_released,
	.locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
	.get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int behavior_drag_scroll_init(const struct device *dev) { return 0; }

#define DRAG_SCROLL_INST(n)                                                                        \
	BEHAVIOR_DT_INST_DEFINE(n, behavior_drag_scroll_init, NULL, NULL, NULL, POST_KERNEL,       \
				CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_drag_scroll_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DRAG_SCROLL_INST)
