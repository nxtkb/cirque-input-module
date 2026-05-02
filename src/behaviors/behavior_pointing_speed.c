/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_pointing_speed

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#if IS_ENABLED(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/events/trackpad_status_changed.h>
#include <zmk/pointing_speed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct pointing_speed_state {
	uint8_t index;
	size_t count;
};

static struct pointing_speed_state pointer_speed_state = {
	.index = 1,
};
static struct pointing_speed_state scroll_speed_state = {
	.index = 1,
};

#if IS_ENABLED(CONFIG_SETTINGS)
#define POINTING_SPEED_SETTINGS_PATH "pointing_speed/state"

struct pointing_speed_settings {
	uint8_t pointer_index;
	uint8_t scroll_index;
};
#endif

static struct pointing_speed_state *speed_state_for_target(enum zmk_pointing_speed_target target)
{
	switch (target) {
	case ZMK_POINTING_SPEED_TARGET_SCROLL:
		return &scroll_speed_state;
	case ZMK_POINTING_SPEED_TARGET_POINTER:
	default:
		return &pointer_speed_state;
	}
}

static uint8_t clamp_speed_index(uint8_t index, size_t count)
{
	if (count > 0 && index >= count) {
		return count - 1;
	}

	return index;
}

uint8_t zmk_pointing_speed_get_index(enum zmk_pointing_speed_target target)
{
	struct pointing_speed_state *state = speed_state_for_target(target);

	return clamp_speed_index(state->index, state->count);
}

#if IS_ENABLED(CONFIG_SETTINGS)
static int zmk_pointing_speed_save(void)
{
	struct pointing_speed_settings settings = {
		.pointer_index = zmk_pointing_speed_get_index(ZMK_POINTING_SPEED_TARGET_POINTER),
		.scroll_index = zmk_pointing_speed_get_index(ZMK_POINTING_SPEED_TARGET_SCROLL),
	};

	int rc = settings_save_one(POINTING_SPEED_SETTINGS_PATH, &settings, sizeof(settings));
	if (rc) {
		LOG_ERR("Failed to save pointing speed settings: %d", rc);
	}

	return rc < 0 ? rc : 0;
}

static int pointing_speed_settings_set(const char *name, size_t len, settings_read_cb read_cb,
				       void *cb_arg)
{
	const char *next;

	if (settings_name_steq(name, "state", &next) && !next) {
		struct pointing_speed_settings settings;

		if (len != sizeof(settings)) {
			return -EINVAL;
		}

		int rc = read_cb(cb_arg, &settings, sizeof(settings));
		if (rc < 0) {
			return rc;
		}

		pointer_speed_state.index =
			clamp_speed_index(settings.pointer_index, pointer_speed_state.count);
		scroll_speed_state.index =
			clamp_speed_index(settings.scroll_index, scroll_speed_state.count);

		LOG_INF("Loaded pointing speed settings: pointer=%u scroll=%u",
			pointer_speed_state.index, scroll_speed_state.index);

		return 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(pointing_speed, "pointing_speed", NULL, pointing_speed_settings_set,
			       NULL, NULL);
#endif

void zmk_pointing_speed_set_count(enum zmk_pointing_speed_target target, size_t count)
{
	struct pointing_speed_state *state = speed_state_for_target(target);

	state->count = count;
	state->index = clamp_speed_index(state->index, state->count);
}

void zmk_pointing_speed_set_initial_index(enum zmk_pointing_speed_target target, uint8_t index)
{
	struct pointing_speed_state *state = speed_state_for_target(target);

	state->index = clamp_speed_index(index, state->count);
}

void zmk_pointing_speed_adjust(enum zmk_pointing_speed_target target,
			       enum zmk_pointing_speed_action action)
{
	struct pointing_speed_state *state = speed_state_for_target(target);

	if (action == ZMK_POINTING_SPEED_ACTION_PREV) {
		if (state->count > 0) {
			state->index = state->index > 0 ? state->index - 1 : state->count - 1;
		} else {
			state->index--;
		}
	} else {
		if (state->count > 0) {
			state->index = state->index + 1 < state->count ? state->index + 1 : 0;
		} else {
			state->index++;
		}
	}

#if IS_ENABLED(CONFIG_SETTINGS)
	zmk_pointing_speed_save();
#endif

	raise_trackpad_status_changed();
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
