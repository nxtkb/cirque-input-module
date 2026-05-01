/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_cirque_mode

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/cirque_mode.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define CIRQUE_MODE_APPLY_ONE(node_id)                                                            \
	do {                                                                                       \
		const struct device *dev = DEVICE_DT_GET(node_id);                                 \
		if (!device_is_ready(dev)) {                                                       \
			break;                                                                     \
		}                                                                                  \
		int rc = zmk_cirque_mode_apply(dev, binding->param1);                              \
		if (rc) {                                                                          \
			return rc;                                                                 \
		}                                                                                  \
	} while (0);

static int on_cirque_mode_binding_pressed(struct zmk_behavior_binding *binding,
					  struct zmk_behavior_binding_event event)
{
	ARG_UNUSED(event);

#if DT_HAS_COMPAT_STATUS_OKAY(cirque_pinnacle2)
	DT_FOREACH_STATUS_OKAY(cirque_pinnacle2, CIRQUE_MODE_APPLY_ONE)
	return ZMK_BEHAVIOR_OPAQUE;
#else
	ARG_UNUSED(binding);
	return ZMK_BEHAVIOR_OPAQUE;
#endif
}

static int on_cirque_mode_binding_released(struct zmk_behavior_binding *binding,
					   struct zmk_behavior_binding_event event)
{
	ARG_UNUSED(binding);
	ARG_UNUSED(event);

	return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_cirque_mode_driver_api = {
	.binding_pressed = on_cirque_mode_binding_pressed,
	.binding_released = on_cirque_mode_binding_released,
	.locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
	.get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int behavior_cirque_mode_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

#define CIRQUE_MODE_INST(n)                                                                        \
	BEHAVIOR_DT_INST_DEFINE(n, behavior_cirque_mode_init, NULL, NULL, NULL, POST_KERNEL,       \
				CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                \
				&behavior_cirque_mode_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CIRQUE_MODE_INST)
