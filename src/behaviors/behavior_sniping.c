/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_sniping

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#endif
#include <zmk/sniping.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static bool sniping_enabled;
static bool left_shift_pressed;
static bool right_shift_pressed;

void zmk_sniping_set_enabled(bool enabled) { sniping_enabled = enabled; }

bool zmk_sniping_is_enabled(void) { return sniping_enabled; }

bool zmk_sniping_is_boosted(void)
{
	return sniping_enabled && (left_shift_pressed || right_shift_pressed);
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
static int sniping_keycode_state_changed_listener(const zmk_event_t *eh)
{
	const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);

	if (ev == NULL || ev->usage_page != HID_USAGE_KEY) {
		return ZMK_EV_EVENT_BUBBLE;
	}

	switch (ev->keycode) {
	case HID_USAGE_KEY_KEYBOARD_LEFTSHIFT:
		left_shift_pressed = ev->state;
		break;
	case HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT:
		right_shift_pressed = ev->state;
		break;
	default:
		break;
	}

	return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_sniping, sniping_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_sniping, zmk_keycode_state_changed);
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
static int on_sniping_binding_pressed(struct zmk_behavior_binding *binding,
				      struct zmk_behavior_binding_event event)
{
	zmk_sniping_set_enabled(true);
	return ZMK_BEHAVIOR_OPAQUE;
}

static int on_sniping_binding_released(struct zmk_behavior_binding *binding,
				       struct zmk_behavior_binding_event event)
{
	zmk_sniping_set_enabled(false);
	return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_sniping_driver_api = {
	.binding_pressed = on_sniping_binding_pressed,
	.binding_released = on_sniping_binding_released,
	.locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
	.get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int behavior_sniping_init(const struct device *dev) { return 0; }

#define SNIPING_INST(n)                                                                            \
	BEHAVIOR_DT_INST_DEFINE(n, behavior_sniping_init, NULL, NULL, NULL, POST_KERNEL,           \
				CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_sniping_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SNIPING_INST)
#endif
