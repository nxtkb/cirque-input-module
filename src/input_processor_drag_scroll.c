/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_drag_scroll

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>
#include <zmk/drag_scroll.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#endif
#include <zmk/pointing_speed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define NATIVE_WHEEL_SPEED_BOOST 8
#define INERTIA_SCALE 1000
#define CAN_SEND_INERTIA_REPORTS                                                                  \
	(IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT))

enum native_wheel_axis {
	NATIVE_WHEEL_AXIS_VERTICAL,
	NATIVE_WHEEL_AXIS_HORIZONTAL,
	NATIVE_WHEEL_AXIS_HORIZONTAL_WHEN_ENABLED,
};

struct drag_scroll_config {
	size_t speed_count;
	uint8_t initial_speed_index;
	enum native_wheel_axis native_wheel_axis;
	bool invert_scroll;
	uint16_t scroll_min_step;
	uint16_t scroll_max_delta;
	uint16_t scroll_curve_deadzone;
	uint16_t scroll_curve_accel_multiplier;
	uint16_t scroll_curve_accel_divisor;
	bool scroll_inertia;
	uint16_t scroll_inertia_interval_ms;
	uint8_t scroll_inertia_decay_percent;
	const uint32_t *speed_multipliers;
	const uint32_t *speed_divisors;
};

struct drag_scroll_data {
	const struct device *dev;
	struct k_work_delayable inertia_work;
	int32_t inertia_hwheel;
	int32_t inertia_wheel;
};

static int32_t clamp_to_i16(int64_t value)
{
	if (value > INT16_MAX) {
		return INT16_MAX;
	}

	if (value < INT16_MIN) {
		return INT16_MIN;
	}

	return (int32_t)value;
}

static int64_t abs64(int64_t value) { return value < 0 ? -value : value; }

static int64_t apply_scroll_curve(int64_t value, const struct drag_scroll_config *cfg)
{
	if (value == 0 || cfg->scroll_curve_accel_multiplier == 0) {
		return value;
	}

	int64_t magnitude = abs64(value);

	if (magnitude <= cfg->scroll_curve_deadzone) {
		return value;
	}

	uint16_t accel_divisor = cfg->scroll_curve_accel_divisor;
	if (accel_divisor == 0) {
		accel_divisor = 1;
	}

	int64_t over_deadzone = magnitude - cfg->scroll_curve_deadzone;
	int64_t accelerated =
		magnitude + (over_deadzone * over_deadzone * cfg->scroll_curve_accel_multiplier) /
				    accel_divisor;

	return value < 0 ? -accelerated : accelerated;
}

static int32_t scale_scroll_value(struct input_event *event, uint32_t mul, uint32_t div,
				  const struct drag_scroll_config *cfg,
				  struct zmk_input_processor_state *state)
{
	if (div == 0) {
		div = 1;
	}

	int64_t value_mul = (int64_t)event->value * (int64_t)mul;

	if (state != NULL && state->remainder != NULL) {
		value_mul += *state->remainder;
	}

	int64_t linear_scaled = value_mul / (int64_t)div;
	int64_t scaled = apply_scroll_curve(linear_scaled, cfg);

	if (cfg->scroll_min_step > 0 && abs64(scaled) < cfg->scroll_min_step) {
		if (state != NULL && state->remainder != NULL) {
			*state->remainder = clamp_to_i16(value_mul);
		}
		return 0;
	}

	if (cfg->scroll_max_delta > 0 && abs64(scaled) > cfg->scroll_max_delta) {
		scaled = scaled < 0 ? -(int64_t)cfg->scroll_max_delta : cfg->scroll_max_delta;
		if (state != NULL && state->remainder != NULL) {
			*state->remainder = 0;
		}
		return clamp_to_i16(scaled);
	}

	if (state != NULL && state->remainder != NULL) {
		*state->remainder = clamp_to_i16(value_mul - (linear_scaled * (int64_t)div));
	}

	return clamp_to_i16(scaled);
}

static int32_t maybe_invert_scroll(int32_t value, const struct drag_scroll_config *cfg)
{
	return cfg->invert_scroll ? -value : value;
}

static void clear_inertia(struct drag_scroll_data *data)
{
	data->inertia_hwheel = 0;
	data->inertia_wheel = 0;
}

#if CAN_SEND_INERTIA_REPORTS
static int32_t abs32(int32_t value) { return value < 0 ? -value : value; }

static int16_t fixed_to_i16_delta(int32_t value)
{
	if (abs32(value) < INERTIA_SCALE) {
		return 0;
	}

	return clamp_to_i16(value / INERTIA_SCALE);
}

static void stop_inertia(struct drag_scroll_data *data)
{
	k_work_cancel_delayable(&data->inertia_work);
	clear_inertia(data);
}

static void schedule_inertia(struct drag_scroll_data *data, int16_t hwheel, int16_t wheel)
{
	const struct drag_scroll_config *cfg = data->dev->config;

	if (!cfg->scroll_inertia || !zmk_drag_scroll_is_enabled()) {
		clear_inertia(data);
		return;
	}

	if (hwheel == 0 && wheel == 0) {
		return;
	}

	k_work_cancel_delayable(&data->inertia_work);
	data->inertia_hwheel = (int32_t)hwheel * INERTIA_SCALE;
	data->inertia_wheel = (int32_t)wheel * INERTIA_SCALE;
	k_work_schedule(&data->inertia_work, K_MSEC(cfg->scroll_inertia_interval_ms));
}

static void inertia_work_cb(struct k_work *work)
{
	struct k_work_delayable *d_work = k_work_delayable_from_work(work);
	struct drag_scroll_data *data = CONTAINER_OF(d_work, struct drag_scroll_data, inertia_work);
	const struct drag_scroll_config *cfg = data->dev->config;

	if (!cfg->scroll_inertia || !zmk_drag_scroll_is_enabled()) {
		stop_inertia(data);
		return;
	}

	data->inertia_hwheel =
		((int64_t)data->inertia_hwheel * cfg->scroll_inertia_decay_percent) / 100;
	data->inertia_wheel =
		((int64_t)data->inertia_wheel * cfg->scroll_inertia_decay_percent) / 100;

	int16_t hwheel = fixed_to_i16_delta(data->inertia_hwheel);
	int16_t wheel = fixed_to_i16_delta(data->inertia_wheel);

	if (hwheel == 0 && wheel == 0) {
		clear_inertia(data);
		return;
	}

	zmk_hid_mouse_scroll_set(hwheel, wheel);
	zmk_endpoint_send_mouse_report();
	zmk_hid_mouse_scroll_set(0, 0);

	k_work_schedule(&data->inertia_work, K_MSEC(cfg->scroll_inertia_interval_ms));
}
#else
static void schedule_inertia(struct drag_scroll_data *data, int16_t hwheel, int16_t wheel)
{
	ARG_UNUSED(hwheel);
	ARG_UNUSED(wheel);
	clear_inertia(data);
}
#endif

static int drag_scroll_handle_event(const struct device *dev, struct input_event *event,
				    uint32_t param1, uint32_t param2,
				    struct zmk_input_processor_state *state)
{
	const struct drag_scroll_config *cfg = dev->config;
	struct drag_scroll_data *data = dev->data;

	ARG_UNUSED(param1);
	ARG_UNUSED(param2);

	if (event->type != INPUT_EV_REL) {
		return ZMK_INPUT_PROC_CONTINUE;
	}

	uint8_t speed_index =
		zmk_pointing_speed_get_index(ZMK_POINTING_SPEED_TARGET_SCROLL) % cfg->speed_count;

	switch (event->code) {
	case INPUT_REL_WHEEL:
		if (cfg->native_wheel_axis == NATIVE_WHEEL_AXIS_HORIZONTAL ||
		    (cfg->native_wheel_axis == NATIVE_WHEEL_AXIS_HORIZONTAL_WHEN_ENABLED &&
		     zmk_drag_scroll_is_enabled())) {
			event->code = INPUT_REL_HWHEEL;
		}
		__fallthrough;
	case INPUT_REL_HWHEEL: {
		uint32_t native_mul =
			cfg->speed_multipliers[speed_index] * NATIVE_WHEEL_SPEED_BOOST;
		uint32_t native_div = cfg->speed_divisors[speed_index];
		event->value = scale_scroll_value(event, native_mul, native_div, cfg, state);
		event->value = maybe_invert_scroll(event->value, cfg);
		if (event->code == INPUT_REL_HWHEEL) {
			schedule_inertia(data, event->value, 0);
		} else {
			schedule_inertia(data, 0, event->value);
		}
		break;
	}
	case INPUT_REL_X: {
		if (!zmk_drag_scroll_is_enabled()) {
			break;
		}
		event->code = INPUT_REL_HWHEEL;
		uint32_t x_mul = cfg->speed_multipliers[speed_index];
		uint32_t x_div = cfg->speed_divisors[speed_index];
		event->value = scale_scroll_value(event, x_mul, x_div, cfg, state);
		event->value = maybe_invert_scroll(event->value, cfg);
		schedule_inertia(data, event->value, 0);
		break;
	}
	case INPUT_REL_Y: {
		if (!zmk_drag_scroll_is_enabled()) {
			break;
		}
		event->code = INPUT_REL_WHEEL;
		uint32_t y_mul = cfg->speed_multipliers[speed_index];
		uint32_t y_div = cfg->speed_divisors[speed_index];
		event->value = scale_scroll_value(event, y_mul, y_div, cfg, state);
		event->value = -event->value;
		event->value = maybe_invert_scroll(event->value, cfg);
		schedule_inertia(data, 0, event->value);
		break;
	}
	default:
		break;
	}

	return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api drag_scroll_driver_api = {
	.handle_event = drag_scroll_handle_event,
};

static int drag_scroll_init(const struct device *dev)
{
	const struct drag_scroll_config *cfg = dev->config;
	struct drag_scroll_data *data = dev->data;

	data->dev = dev;
#if CAN_SEND_INERTIA_REPORTS
	k_work_init_delayable(&data->inertia_work, inertia_work_cb);
#endif

	zmk_pointing_speed_set_count(ZMK_POINTING_SPEED_TARGET_SCROLL, cfg->speed_count);
	zmk_pointing_speed_set_initial_index(ZMK_POINTING_SPEED_TARGET_SCROLL,
					     cfg->initial_speed_index);
	return 0;
}

#define DRAG_SCROLL_PROCESSOR_INST(n)                                                              \
	BUILD_ASSERT(DT_INST_PROP_LEN(n, speed_multipliers) ==                                  \
			     DT_INST_PROP_LEN(n, speed_divisors),                                  \
		     "speed-multipliers and speed-divisors must have the same length");             \
	BUILD_ASSERT(DT_INST_PROP_LEN(n, speed_multipliers) > 0,                                 \
		     "at least one speed level is required");                                      \
	static const uint32_t drag_scroll_speed_multipliers_##n[] =                               \
		DT_INST_PROP(n, speed_multipliers);                                              \
	static const uint32_t drag_scroll_speed_divisors_##n[] = DT_INST_PROP(n, speed_divisors); \
	static struct drag_scroll_data drag_scroll_data_##n;                                      \
	static const struct drag_scroll_config drag_scroll_config_##n = {                         \
		.speed_count = DT_INST_PROP_LEN(n, speed_multipliers),                             \
		.initial_speed_index = DT_INST_PROP(n, initial_speed_index),                       \
		.native_wheel_axis = DT_INST_ENUM_IDX(n, native_wheel_axis),                       \
		.invert_scroll = DT_INST_PROP(n, invert_scroll),                                   \
		.scroll_min_step = DT_INST_PROP(n, scroll_min_step),                               \
		.scroll_max_delta = DT_INST_PROP(n, scroll_max_delta),                             \
		.scroll_curve_deadzone = DT_INST_PROP(n, scroll_curve_deadzone),                   \
		.scroll_curve_accel_multiplier =                                                   \
			DT_INST_PROP(n, scroll_curve_accel_multiplier),                            \
		.scroll_curve_accel_divisor = DT_INST_PROP(n, scroll_curve_accel_divisor),         \
		.scroll_inertia = DT_INST_PROP(n, scroll_inertia),                                 \
		.scroll_inertia_interval_ms = DT_INST_PROP(n, scroll_inertia_interval_ms),         \
		.scroll_inertia_decay_percent = DT_INST_PROP(n, scroll_inertia_decay_percent),     \
		.speed_multipliers = drag_scroll_speed_multipliers_##n,                            \
		.speed_divisors = drag_scroll_speed_divisors_##n,                                  \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, drag_scroll_init, NULL, &drag_scroll_data_##n,                    \
			      &drag_scroll_config_##n, POST_KERNEL,                                    \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &drag_scroll_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DRAG_SCROLL_PROCESSOR_INST)
