/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_cirque_mode

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#if IS_ENABLED(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/cirque_mode.h>
#include <zmk/event_manager.h>
#include <zmk/events/trackpad_status_changed.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/events/split_peripheral_status_changed.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(cirque_pinnacle2)
static bool cirque_relative_mode = true;
#else
static bool cirque_relative_mode = false;
#endif
static bool cirque_mode_known = DT_HAS_COMPAT_STATUS_OKAY(cirque_pinnacle2);

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

static void zmk_cirque_mode_set_confirmed(bool relative_mode)
{
	bool changed = !cirque_mode_known || cirque_relative_mode != relative_mode;

	cirque_relative_mode = relative_mode;
	cirque_mode_known = true;

	if (changed) {
		raise_trackpad_status_changed();
	}
}

static void zmk_cirque_mode_mark_unknown(void)
{
	if (!cirque_mode_known) {
		return;
	}

	cirque_mode_known = false;
	raise_trackpad_status_changed();
}

int zmk_cirque_mode_report(const struct device *dev, bool relative_mode)
{
	zmk_cirque_mode_set_confirmed(relative_mode);

	if (dev == NULL) {
		return 0;
	}

	int rc = input_report(dev, ZMK_CIRQUE_MODE_STATUS_INPUT_TYPE, ZMK_CIRQUE_MODE_STATUS_INPUT_CODE,
			      relative_mode ? ZMK_CIRQUE_MODE_STATUS_INPUT_VALUE_RELATIVE
					    : ZMK_CIRQUE_MODE_STATUS_INPUT_VALUE_ABSOLUTE,
			      false, K_NO_WAIT);
	if (rc) {
		LOG_WRN("Failed to report confirmed Cirque mode: %d", rc);
	}

	return rc < 0 ? rc : 0;
}

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
	bool applied = false;

	int rc = zmk_cirque_mode_apply_all(ZMK_CIRQUE_MODE_ACTION_RELATIVE, NULL, &applied);
	if (rc) {
		return rc;
	}

	if (!applied) {
		zmk_cirque_mode_mark_unknown();
		return 0;
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
		case CIRQUE_MODE_SETTING_ABSOLUTE: {
			bool applied = false;
			enum zmk_cirque_mode_action action =
				mode == CIRQUE_MODE_SETTING_RELATIVE
					? ZMK_CIRQUE_MODE_ACTION_RELATIVE
					: ZMK_CIRQUE_MODE_ACTION_ABSOLUTE;

			rc = zmk_cirque_mode_apply_all(action, NULL, &applied);
			if (rc) {
				return rc;
			}

			if (!applied) {
				zmk_cirque_mode_mark_unknown();
			}

			return 0;
		}
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

	if (!applied) {
		zmk_cirque_mode_mark_unknown();
		return ZMK_BEHAVIOR_OPAQUE;
	}

	zmk_cirque_mode_set_confirmed(relative_mode);

#if IS_ENABLED(CONFIG_SETTINGS)
	zmk_cirque_mode_save(relative_mode);
#endif

	return ZMK_BEHAVIOR_OPAQUE;
}

static int on_cirque_mode_binding_released(struct zmk_behavior_binding *binding,
					   struct zmk_behavior_binding_event event)
{
	ARG_UNUSED(binding);
	ARG_UNUSED(event);

	return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_INPUT_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static void cirque_mode_status_input_listener(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != ZMK_CIRQUE_MODE_STATUS_INPUT_TYPE ||
	    evt->code != ZMK_CIRQUE_MODE_STATUS_INPUT_CODE) {
		return;
	}

	switch (evt->value) {
	case ZMK_CIRQUE_MODE_STATUS_INPUT_VALUE_RELATIVE:
		zmk_cirque_mode_set_confirmed(true);
		break;
	case ZMK_CIRQUE_MODE_STATUS_INPUT_VALUE_ABSOLUTE:
		zmk_cirque_mode_set_confirmed(false);
		break;
	default:
		LOG_WRN("Ignoring invalid Cirque mode status value: %d", evt->value);
		break;
	}
}

INPUT_CALLBACK_DEFINE(NULL, cirque_mode_status_input_listener, NULL);
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static int cirque_mode_split_central_status_listener(const zmk_event_t *eh)
{
	const struct zmk_split_peripheral_status_changed *ev =
		as_zmk_split_peripheral_status_changed(eh);

	if (ev != NULL && !ev->connected) {
		zmk_cirque_mode_mark_unknown();
	}

	return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(cirque_mode_split_central_status, cirque_mode_split_central_status_listener);
ZMK_SUBSCRIPTION(cirque_mode_split_central_status, zmk_split_peripheral_status_changed);
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(cirque_pinnacle2)
#define CIRQUE_MODE_REPORT_ONE(node_id)                                                            \
	do {                                                                                        \
		const struct device *dev = DEVICE_DT_GET(node_id);                                  \
		if (device_is_ready(dev)) {                                                        \
			zmk_cirque_mode_report(dev, zmk_cirque_mode_is_relative(dev));             \
		}                                                                                   \
	} while (0);

static void zmk_cirque_mode_report_all(void)
{
	DT_FOREACH_STATUS_OKAY(cirque_pinnacle2, CIRQUE_MODE_REPORT_ONE)
}
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) &&                 \
	DT_HAS_COMPAT_STATUS_OKAY(cirque_pinnacle2)
#define CIRQUE_MODE_RECONNECT_REPORT_DELAY K_MSEC(500)
#define CIRQUE_MODE_RECONNECT_REPORT_RETRIES 4

static uint8_t cirque_mode_reconnect_reports_remaining;
static void cirque_mode_reconnect_report_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(cirque_mode_reconnect_report_work, cirque_mode_reconnect_report_work_cb);

static void cirque_mode_reconnect_report_work_cb(struct k_work *work)
{
	ARG_UNUSED(work);

	if (cirque_mode_reconnect_reports_remaining == 0) {
		return;
	}

	zmk_cirque_mode_report_all();
	cirque_mode_reconnect_reports_remaining--;

	if (cirque_mode_reconnect_reports_remaining > 0) {
		k_work_reschedule(&cirque_mode_reconnect_report_work,
				  CIRQUE_MODE_RECONNECT_REPORT_DELAY);
	}
}

static int cirque_mode_split_peripheral_status_listener(const zmk_event_t *eh)
{
	const struct zmk_split_peripheral_status_changed *ev =
		as_zmk_split_peripheral_status_changed(eh);

	if (ev == NULL) {
		return ZMK_EV_EVENT_BUBBLE;
	}

	if (ev->connected) {
		zmk_cirque_mode_report_all();
		cirque_mode_reconnect_reports_remaining = CIRQUE_MODE_RECONNECT_REPORT_RETRIES;
		k_work_reschedule(&cirque_mode_reconnect_report_work,
				  CIRQUE_MODE_RECONNECT_REPORT_DELAY);
	} else {
		cirque_mode_reconnect_reports_remaining = 0;
		k_work_cancel_delayable(&cirque_mode_reconnect_report_work);
	}

	return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(cirque_mode_split_peripheral_status, cirque_mode_split_peripheral_status_listener);
ZMK_SUBSCRIPTION(cirque_mode_split_peripheral_status, zmk_split_peripheral_status_changed);
#endif

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
