/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_pointer_speed

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>
#include <zmk/drag_scroll.h>
#include <zmk/pointing_speed.h>
#include <zmk/pointing_speed_math.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

enum pointer_curve {
	POINTER_CURVE_LINEAR,
	POINTER_CURVE_ADAPTIVE,
};

struct pointer_speed_config {
	uint8_t initial_speed_position;
	enum pointer_curve pointer_curve;
	uint16_t pointer_curve_deadzone;
	uint16_t pointer_curve_accel_multiplier;
	uint16_t pointer_curve_accel_divisor;
	uint16_t pointer_curve_max_delta;
	uint32_t one_x_multiplier;
	uint32_t one_x_divisor;
	uint16_t min_percent;
	uint16_t max_percent;
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

static int32_t scale_value(struct input_event *event, const struct pointer_speed_config *cfg,
			   struct zmk_input_processor_state *state)
{
	uint32_t div = cfg->one_x_divisor;
	if (div == 0U) {
		div = 1U;
	}

	uint32_t speed_q16 = zmk_pointing_speed_get_multiplier_q16(ZMK_POINTING_SPEED_TARGET_POINTER);
	int64_t divisor = (int64_t)div * ZMK_POINTING_SPEED_Q16_SCALE;
	int64_t value_mul =
		(int64_t)event->value * (int64_t)cfg->one_x_multiplier * (int64_t)speed_q16;

	if (state != NULL && state->remainder != NULL) {
		value_mul += *state->remainder;
	}

	int64_t scaled = value_mul / divisor;

	if (state != NULL && state->remainder != NULL) {
		*state->remainder = value_mul - (scaled * divisor);
	}

	return clamp_to_i16(scaled);
}

static int32_t apply_pointer_curve(int32_t value, const struct pointer_speed_config *cfg)
{
	if (cfg->pointer_curve != POINTER_CURVE_ADAPTIVE || value == 0) {
		return value;
	}

	int64_t magnitude = value < 0 ? -(int64_t)value : value;

	if (magnitude <= cfg->pointer_curve_deadzone) {
		return value;
	}

	uint16_t accel_divisor = cfg->pointer_curve_accel_divisor;
	if (accel_divisor == 0) {
		accel_divisor = 1;
	}

	int64_t over_deadzone = magnitude - cfg->pointer_curve_deadzone;
	int64_t accelerated =
		magnitude + (over_deadzone * over_deadzone * cfg->pointer_curve_accel_multiplier) /
				    accel_divisor;

	if (cfg->pointer_curve_max_delta > 0 && accelerated > cfg->pointer_curve_max_delta) {
		accelerated = cfg->pointer_curve_max_delta;
	}

	if (value < 0) {
		accelerated = -accelerated;
	}

	return clamp_to_i16(accelerated);
}

static int pointer_speed_handle_event(const struct device *dev, struct input_event *event,
				      uint32_t param1, uint32_t param2,
				      struct zmk_input_processor_state *state)
{
	const struct pointer_speed_config *cfg = dev->config;

	ARG_UNUSED(param1);
	ARG_UNUSED(param2);

	if (event->type != INPUT_EV_REL) {
		return ZMK_INPUT_PROC_CONTINUE;
	}

	switch (event->code) {
	case INPUT_REL_X:
	case INPUT_REL_Y: {
		if (zmk_drag_scroll_is_enabled()) {
			break;
		}
		event->value = scale_value(event, cfg, state);
		event->value = apply_pointer_curve(event->value, cfg);
		break;
	}
	default:
		break;
	}

	return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api pointer_speed_driver_api = {
	.handle_event = pointer_speed_handle_event,
};

static int pointer_speed_init(const struct device *dev)
{
	const struct pointer_speed_config *cfg = dev->config;

	zmk_pointing_speed_set_initial_position(ZMK_POINTING_SPEED_TARGET_POINTER,
						cfg->initial_speed_position);
	zmk_pointing_speed_set_range(ZMK_POINTING_SPEED_TARGET_POINTER, cfg->min_percent,
				     cfg->max_percent);
	return 0;
}

#define POINTER_SPEED_PROCESSOR_INST(n)                                                            \
	static const struct pointer_speed_config pointer_speed_config_##n = {                       \
		.initial_speed_position = DT_INST_PROP(n, initial_speed_position),                 \
		.pointer_curve = DT_INST_ENUM_IDX(n, pointer_curve),                               \
		.pointer_curve_deadzone = DT_INST_PROP(n, pointer_curve_deadzone),                 \
		.pointer_curve_accel_multiplier = DT_INST_PROP(n, pointer_curve_accel_multiplier), \
		.pointer_curve_accel_divisor = DT_INST_PROP(n, pointer_curve_accel_divisor),       \
		.pointer_curve_max_delta = DT_INST_PROP(n, pointer_curve_max_delta),               \
		.one_x_multiplier = DT_INST_PROP(n, one_x_multiplier),                             \
		.one_x_divisor = DT_INST_PROP(n, one_x_divisor),                                   \
		.min_percent = DT_INST_PROP(n, min_percent),                                       \
		.max_percent = DT_INST_PROP(n, max_percent),                                       \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, pointer_speed_init, NULL, NULL, &pointer_speed_config_##n,        \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                     \
			      &pointer_speed_driver_api);

DT_INST_FOREACH_STATUS_OKAY(POINTER_SPEED_PROCESSOR_INST)
