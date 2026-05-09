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
#include <zmk/pointing_speed_math.h>

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
	uint8_t initial_speed_position;
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
	uint32_t one_x_multiplier;
	uint32_t one_x_divisor;
	uint16_t min_percent;
	uint16_t max_percent;
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
	if (div == 0U) {
		div = 1U;
	}

	uint32_t speed_q16 = zmk_pointing_speed_get_multiplier_q16(ZMK_POINTING_SPEED_TARGET_SCROLL);
	int64_t divisor = (int64_t)div * ZMK_POINTING_SPEED_Q16_SCALE;
	int64_t value_mul = (int64_t)event->value * (int64_t)mul * (int64_t)speed_q16;

	if (state != NULL && state->remainder != NULL) {
		value_mul += *state->remainder;
	}

	int64_t linear_scaled = value_mul / divisor;
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
		*state->remainder = clamp_to_i16(value_mul - (linear_scaled * divisor));
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

	switch (event->code) {
	case INPUT_REL_WHEEL:
		if (cfg->native_wheel_axis == NATIVE_WHEEL_AXIS_HORIZONTAL ||
		    (cfg->native_wheel_axis == NATIVE_WHEEL_AXIS_HORIZONTAL_WHEN_ENABLED &&
		     zmk_drag_scroll_is_enabled())) {
			event->code = INPUT_REL_HWHEEL;
		}
		__fallthrough;
	case INPUT_REL_HWHEEL: {
		uint32_t native_mul = cfg->one_x_multiplier * NATIVE_WHEEL_SPEED_BOOST;
		uint32_t native_div = cfg->one_x_divisor;
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
		uint32_t x_mul = cfg->one_x_multiplier;
		uint32_t x_div = cfg->one_x_divisor;
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
		uint32_t y_mul = cfg->one_x_multiplier;
		uint32_t y_div = cfg->one_x_divisor;
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

	zmk_pointing_speed_set_initial_position(ZMK_POINTING_SPEED_TARGET_SCROLL,
						cfg->initial_speed_position);
	zmk_pointing_speed_set_range(ZMK_POINTING_SPEED_TARGET_SCROLL, cfg->min_percent,
				     cfg->max_percent);
	return 0;
}

#define DRAG_SCROLL_PROCESSOR_INST(n)                                                              \
	static struct drag_scroll_data drag_scroll_data_##n;                                      \
	static const struct drag_scroll_config drag_scroll_config_##n = {                         \
		.initial_speed_position = DT_INST_PROP(n, initial_speed_position),                 \
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
		.one_x_multiplier = DT_INST_PROP(n, one_x_multiplier),                             \
		.one_x_divisor = DT_INST_PROP(n, one_x_divisor),                                   \
		.min_percent = DT_INST_PROP(n, min_percent),                                       \
		.max_percent = DT_INST_PROP(n, max_percent),                                       \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, drag_scroll_init, NULL, &drag_scroll_data_##n,                    \
			      &drag_scroll_config_##n, POST_KERNEL,                                    \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &drag_scroll_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DRAG_SCROLL_PROCESSOR_INST)
