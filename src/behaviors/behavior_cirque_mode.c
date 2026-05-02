/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_cirque_mode

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#if IS_ENABLED(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/cirque_mode.h>
#include <zmk/events/trackpad_status_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static bool cirque_relative_mode = false;
static bool cirque_mode_known = true;

static bool cirque_mode_from_action(enum zmk_cirque_mode_action action, bool fallback_relative_mode)
{
	switch (action) {
	case ZMK_CIRQUE_MODE_ACTION_RELATIVE:
		return true;
	case ZMK_CIRQUE_MODE_ACTION_ABSOLUTE:
		return false;
	case ZMK_CIRQUE_MODE_ACTION_TOGGLE:
	default:
		return !fallback_relative_mode;
	}
}

bool zmk_cirque_mode_get_relative(void) { return cirque_relative_mode; }

bool zmk_cirque_mode_is_known(void) { return cirque_mode_known; }

static int zmk_cirque_mode_apply_all(enum zmk_cirque_mode_action action, bool *relative_mode_out,
				     bool *applied_out);

#if IS_ENABLED(CONFIG_SETTINGS)
#define CIRQUE_MODE_SETTINGS_PATH "cirque/mode"
#define CIRQUE_MODE_SETTINGS_NAME "mode"
#define CIRQUE_MODE_SETTING_RELATIVE 0
#define CIRQUE_MODE_SETTING_ABSOLUTE 1

static int zmk_cirque_mode_save(bool relative_mode)
{
	uint8_t mode = relative_mode ? CIRQUE_MODE_SETTING_RELATIVE : CIRQUE_MODE_SETTING_ABSOLUTE;

	int rc = settings_save_one(CIRQUE_MODE_SETTINGS_PATH, &mode, sizeof(mode));
	if (rc) {
		LOG_ERR("Failed to save Cirque mode setting: %d", rc);
	}

	return rc < 0 ? rc : 0;
}

static int zmk_cirque_mode_reset_to_default(void)
{
	cirque_relative_mode = false;
	cirque_mode_known = true;

	int rc = zmk_cirque_mode_apply_all(ZMK_CIRQUE_MODE_ACTION_ABSOLUTE, NULL, NULL);
	if (rc) {
		return rc;
	}

	return zmk_cirque_mode_save(cirque_relative_mode);
}
#endif

#define CIRQUE_MODE_APPLY_ONE(node_id)                                                            \
	do {                                                                                       \
		const struct device *dev = DEVICE_DT_GET(node_id);                                 \
		if (!device_is_ready(dev)) {                                                       \
			break;                                                                     \
		}                                                                                  \
		int rc = zmk_cirque_mode_apply(dev, action);                                       \
		if (rc) {                                                                          \
			return rc;                                                                 \
		}                                                                                  \
		if (applied_out != NULL) {                                                         \
			*applied_out = true;                                                       \
		}                                                                                  \
		if (relative_mode_out != NULL) {                                                   \
			*relative_mode_out = zmk_cirque_mode_is_relative(dev);                     \
		}                                                                                  \
	} while (0);

static int zmk_cirque_mode_apply_all(enum zmk_cirque_mode_action action, bool *relative_mode_out,
				     bool *applied_out)
{
	bool relative_mode = cirque_mode_from_action(action, cirque_relative_mode);

	if (relative_mode_out != NULL) {
		*relative_mode_out = relative_mode;
	}

#if DT_HAS_COMPAT_STATUS_OKAY(cirque_pinnacle2)
	DT_FOREACH_STATUS_OKAY(cirque_pinnacle2, CIRQUE_MODE_APPLY_ONE)
	return 0;
#else
	ARG_UNUSED(action);
	ARG_UNUSED(relative_mode_out);
	ARG_UNUSED(applied_out);
	return 0;
#endif
}

#if IS_ENABLED(CONFIG_SETTINGS)
static int cirque_mode_settings_set(const char *name, size_t len, settings_read_cb read_cb,
				    void *cb_arg)
{
	const char *next;

	if (settings_name_steq(name, CIRQUE_MODE_SETTINGS_NAME, &next) && !next) {
		uint8_t mode;

		if (len != sizeof(mode)) {
			LOG_WRN("Invalid Cirque mode setting length: %zu", len);
			return zmk_cirque_mode_reset_to_default();
		}

		int rc = read_cb(cb_arg, &mode, sizeof(mode));
		if (rc < 0) {
			return rc;
		}

		switch (mode) {
		case CIRQUE_MODE_SETTING_RELATIVE:
			cirque_relative_mode = true;
			cirque_mode_known = true;
			return zmk_cirque_mode_apply_all(ZMK_CIRQUE_MODE_ACTION_RELATIVE, NULL,
							 NULL);
		case CIRQUE_MODE_SETTING_ABSOLUTE:
			cirque_relative_mode = false;
			cirque_mode_known = true;
			return zmk_cirque_mode_apply_all(ZMK_CIRQUE_MODE_ACTION_ABSOLUTE, NULL,
							 NULL);
		default:
			LOG_WRN("Invalid Cirque mode setting value: %u", mode);
			return zmk_cirque_mode_reset_to_default();
		}
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(cirque_mode, "cirque", NULL, cirque_mode_settings_set, NULL, NULL);
#endif

static int on_cirque_mode_binding_pressed(struct zmk_behavior_binding *binding,
					  struct zmk_behavior_binding_event event)
{
	bool relative_mode = true;
	bool applied = false;
	int rc;

	ARG_UNUSED(event);

	rc = zmk_cirque_mode_apply_all(binding->param1, &relative_mode, &applied);
	if (rc) {
		return rc;
	}

	cirque_relative_mode = relative_mode;
	cirque_mode_known = true;

#if IS_ENABLED(CONFIG_SETTINGS)
	zmk_cirque_mode_save(relative_mode);
#endif

	raise_trackpad_status_changed();

	return ZMK_BEHAVIOR_OPAQUE;
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
