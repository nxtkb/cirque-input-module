/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_pointing_speed

#include <errno.h>
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#if IS_ENABLED(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/endpoints.h>
#include <zmk/events/endpoint_changed.h>
#endif
#include <zmk/events/trackpad_status_changed.h>
#include <zmk/pointing_speed.h>
#include <zmk/pointing_speed_math.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
#define POINTING_SPEED_ENDPOINT_COUNT ZMK_ENDPOINT_COUNT
#define POINTING_SPEED_HAS_ENDPOINTS 1
#else
#define POINTING_SPEED_ENDPOINT_COUNT 1
#define POINTING_SPEED_HAS_ENDPOINTS 0
#endif

struct pointing_speed_state {
	uint8_t position;
	uint32_t multiplier_q16;
	uint16_t min_percent;
	uint16_t max_percent;
	bool has_multiplier_override;
	bool loaded;
};

static struct pointing_speed_state pointer_speed_states[POINTING_SPEED_ENDPOINT_COUNT] = {
	[0 ... POINTING_SPEED_ENDPOINT_COUNT - 1] = {
		.position = ZMK_POINTING_SPEED_DEFAULT_POSITION,
		.multiplier_q16 = ZMK_POINTING_SPEED_Q16_SCALE,
		.min_percent = 10,
		.max_percent = 400,
	},
};

static struct pointing_speed_state scroll_speed_states[POINTING_SPEED_ENDPOINT_COUNT] = {
	[0 ... POINTING_SPEED_ENDPOINT_COUNT - 1] = {
		.position = ZMK_POINTING_SPEED_DEFAULT_POSITION,
		.multiplier_q16 = ZMK_POINTING_SPEED_Q16_SCALE,
		.min_percent = 10,
		.max_percent = 1000,
	},
};

#define POINTING_SPEED_FINE_REPEAT_DELAY_MS 200
#define POINTING_SPEED_FINE_REPEAT_PERIOD_MS 30

struct pointing_speed_repeat_state {
	enum zmk_pointing_speed_target target;
	enum zmk_pointing_speed_action action;
	uint32_t position;
	bool active;
};

static struct pointing_speed_repeat_state repeat_state;

#if IS_ENABLED(CONFIG_SETTINGS)
#define POINTING_SPEED_SETTINGS_PATH "pointing_speed_v3/state"

struct pointing_speed_settings_endpoint {
	uint8_t pointer_position;
	uint8_t scroll_position;
	uint8_t pointer_has_multiplier_override;
	uint8_t scroll_has_multiplier_override;
	uint32_t pointer_multiplier_q16;
	uint32_t scroll_multiplier_q16;
};

struct pointing_speed_settings {
	struct pointing_speed_settings_endpoint endpoints[POINTING_SPEED_ENDPOINT_COUNT];
};
#endif

static int current_endpoint_index(void)
{
#if POINTING_SPEED_HAS_ENDPOINTS
	int index = zmk_endpoint_instance_to_index(zmk_endpoint_get_selected());

	if (index < 0 || index >= POINTING_SPEED_ENDPOINT_COUNT) {
		return 0;
	}

	return index;
#else
	return 0;
#endif
}

static struct pointing_speed_state *speed_state_for_target_and_endpoint(
	enum zmk_pointing_speed_target target, int endpoint_index)
{
	switch (target) {
	case ZMK_POINTING_SPEED_TARGET_SCROLL:
		return &scroll_speed_states[endpoint_index];
	case ZMK_POINTING_SPEED_TARGET_POINTER:
	default:
		return &pointer_speed_states[endpoint_index];
	}
}

static struct pointing_speed_state *speed_state_for_target(enum zmk_pointing_speed_target target)
{
	return speed_state_for_target_and_endpoint(target, current_endpoint_index());
}

static uint8_t clamp_speed_position(uint8_t position)
{
	if (position > ZMK_POINTING_SPEED_MAX_POSITION) {
		return ZMK_POINTING_SPEED_MAX_POSITION;
	}

	return position;
}

uint8_t zmk_pointing_speed_get_position(enum zmk_pointing_speed_target target)
{
	struct pointing_speed_state *state = speed_state_for_target(target);

	return clamp_speed_position(state->position);
}

uint32_t zmk_pointing_speed_get_multiplier_q16(enum zmk_pointing_speed_target target)
{
	struct pointing_speed_state *state = speed_state_for_target(target);

	if (state->has_multiplier_override) {
		return state->multiplier_q16;
	}

	return zmk_pointing_speed_multiplier_q16(zmk_pointing_speed_get_position(target),
						 state->min_percent, state->max_percent);
}

#if IS_ENABLED(CONFIG_SETTINGS)
static void pointing_speed_schedule_save(void);

static int zmk_pointing_speed_save(void)
{
	struct pointing_speed_settings settings = {};

	for (int i = 0; i < POINTING_SPEED_ENDPOINT_COUNT; i++) {
		settings.endpoints[i].pointer_position =
			clamp_speed_position(pointer_speed_states[i].position);
		settings.endpoints[i].scroll_position =
			clamp_speed_position(scroll_speed_states[i].position);
		settings.endpoints[i].pointer_has_multiplier_override =
			pointer_speed_states[i].has_multiplier_override;
		settings.endpoints[i].scroll_has_multiplier_override =
			scroll_speed_states[i].has_multiplier_override;
		settings.endpoints[i].pointer_multiplier_q16 = pointer_speed_states[i].multiplier_q16;
		settings.endpoints[i].scroll_multiplier_q16 = scroll_speed_states[i].multiplier_q16;
	}

	int rc = settings_save_one(POINTING_SPEED_SETTINGS_PATH, &settings, sizeof(settings));
	if (rc) {
		LOG_ERR("Failed to save pointing speed settings: %d", rc);
	}

	return rc < 0 ? rc : 0;
}

static void pointing_speed_save_work_cb(struct k_work *work)
{
	ARG_UNUSED(work);
	zmk_pointing_speed_save();
}

K_WORK_DELAYABLE_DEFINE(pointing_speed_save_work, pointing_speed_save_work_cb);

static void pointing_speed_schedule_save(void)
{
	k_work_reschedule(&pointing_speed_save_work, K_MSEC(500));
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

		for (int i = 0; i < POINTING_SPEED_ENDPOINT_COUNT; i++) {
			pointer_speed_states[i].position =
				clamp_speed_position(settings.endpoints[i].pointer_position);
			pointer_speed_states[i].has_multiplier_override =
				settings.endpoints[i].pointer_has_multiplier_override != 0;
			pointer_speed_states[i].multiplier_q16 = zmk_pointing_speed_adjust_multiplier_q16(
				settings.endpoints[i].pointer_multiplier_q16, 0,
				pointer_speed_states[i].min_percent, pointer_speed_states[i].max_percent);
			pointer_speed_states[i].loaded = true;

			scroll_speed_states[i].position =
				clamp_speed_position(settings.endpoints[i].scroll_position);
			scroll_speed_states[i].has_multiplier_override =
				settings.endpoints[i].scroll_has_multiplier_override != 0;
			scroll_speed_states[i].multiplier_q16 = zmk_pointing_speed_adjust_multiplier_q16(
				settings.endpoints[i].scroll_multiplier_q16, 0,
				scroll_speed_states[i].min_percent, scroll_speed_states[i].max_percent);
			scroll_speed_states[i].loaded = true;
		}

		LOG_INF("Loaded per-endpoint pointing speed settings");

		return 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(pointing_speed, "pointing_speed_v3", NULL,
			       pointing_speed_settings_set, NULL, NULL);
#else
static void pointing_speed_schedule_save(void) {}
#endif

void zmk_pointing_speed_set_initial_position(enum zmk_pointing_speed_target target,
					     uint8_t position)
{
	for (int i = 0; i < POINTING_SPEED_ENDPOINT_COUNT; i++) {
		struct pointing_speed_state *state = speed_state_for_target_and_endpoint(target, i);

		if (!state->loaded) {
			state->position = clamp_speed_position(position);
			state->has_multiplier_override = false;
		}
	}
}

void zmk_pointing_speed_set_range(enum zmk_pointing_speed_target target, uint16_t min_percent,
				  uint16_t max_percent)
{
	for (int i = 0; i < POINTING_SPEED_ENDPOINT_COUNT; i++) {
		struct pointing_speed_state *state = speed_state_for_target_and_endpoint(target, i);

		state->min_percent = min_percent;
		state->max_percent = max_percent;
		if (state->has_multiplier_override) {
			state->multiplier_q16 = zmk_pointing_speed_adjust_multiplier_q16(
				state->multiplier_q16, 0, state->min_percent, state->max_percent);
		}
	}
}

void zmk_pointing_speed_adjust(enum zmk_pointing_speed_target target,
			       enum zmk_pointing_speed_action action)
{
	struct pointing_speed_state *state = speed_state_for_target(target);

	if (action == ZMK_POINTING_SPEED_ACTION_RESET) {
		state->position = ZMK_POINTING_SPEED_DEFAULT_POSITION;
		state->has_multiplier_override = false;
	} else if (action == ZMK_POINTING_SPEED_ACTION_PREV) {
		if (state->position > ZMK_POINTING_SPEED_MIN_POSITION) {
			state->position--;
		}
		state->has_multiplier_override = false;
	} else if (action == ZMK_POINTING_SPEED_ACTION_NEXT) {
		if (state->position < ZMK_POINTING_SPEED_MAX_POSITION) {
			state->position++;
		}
		state->has_multiplier_override = false;
	} else if (action == ZMK_POINTING_SPEED_ACTION_FINE_PREV) {
		state->multiplier_q16 = zmk_pointing_speed_adjust_multiplier_q16(
			zmk_pointing_speed_get_multiplier_q16(target), -1, state->min_percent,
			state->max_percent);
		state->has_multiplier_override = true;
	} else if (action == ZMK_POINTING_SPEED_ACTION_FINE_NEXT) {
		state->multiplier_q16 = zmk_pointing_speed_adjust_multiplier_q16(
			zmk_pointing_speed_get_multiplier_q16(target), 1, state->min_percent,
			state->max_percent);
		state->has_multiplier_override = true;
	}

	pointing_speed_schedule_save();
	raise_trackpad_status_changed();
}

static bool is_fine_action(enum zmk_pointing_speed_action action)
{
	return action == ZMK_POINTING_SPEED_ACTION_FINE_PREV ||
	       action == ZMK_POINTING_SPEED_ACTION_FINE_NEXT;
}

static void pointing_speed_repeat_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(pointing_speed_repeat_work, pointing_speed_repeat_work_cb);

static void pointing_speed_repeat_work_cb(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!repeat_state.active) {
		return;
	}

	zmk_pointing_speed_adjust(repeat_state.target, repeat_state.action);

	if (repeat_state.active) {
		k_work_reschedule(&pointing_speed_repeat_work,
				  K_MSEC(POINTING_SPEED_FINE_REPEAT_PERIOD_MS));
	}
}

static int on_pointing_speed_binding_pressed(struct zmk_behavior_binding *binding,
					     struct zmk_behavior_binding_event event)
{
	zmk_pointing_speed_adjust(binding->param1, binding->param2);

	if (is_fine_action(binding->param2)) {
		repeat_state.target = binding->param1;
		repeat_state.action = binding->param2;
		repeat_state.position = event.position;
		repeat_state.active = true;
		k_work_reschedule(&pointing_speed_repeat_work,
				  K_MSEC(POINTING_SPEED_FINE_REPEAT_DELAY_MS));
	}

	return ZMK_BEHAVIOR_OPAQUE;
}

static int on_pointing_speed_binding_released(struct zmk_behavior_binding *binding,
					      struct zmk_behavior_binding_event event)
{
	if (repeat_state.active && repeat_state.position == event.position) {
		repeat_state.active = false;
		k_work_cancel_delayable(&pointing_speed_repeat_work);
	}

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

#if POINTING_SPEED_HAS_ENDPOINTS
static int pointing_speed_endpoint_changed_listener(const zmk_event_t *eh)
{
	ARG_UNUSED(eh);

	raise_trackpad_status_changed();
	return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(pointing_speed_endpoint_listener, pointing_speed_endpoint_changed_listener);
ZMK_SUBSCRIPTION(pointing_speed_endpoint_listener, zmk_endpoint_changed);
#endif

#define POINTING_SPEED_INST(n)                                                                      \
	BEHAVIOR_DT_INST_DEFINE(n, behavior_pointing_speed_init, NULL, NULL, NULL, POST_KERNEL,     \
				CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                    \
				&behavior_pointing_speed_driver_api);

DT_INST_FOREACH_STATUS_OKAY(POINTING_SPEED_INST)
