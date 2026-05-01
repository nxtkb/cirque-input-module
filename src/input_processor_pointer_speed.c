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

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

enum pointer_curve {
	POINTER_CURVE_LINEAR,
	POINTER_CURVE_ADAPTIVE,
};

struct pointer_speed_config {
	size_t speed_count;
	uint8_t initial_speed_index;
	enum pointer_curve pointer_curve;
	uint16_t pointer_curve_deadzone;
	uint16_t pointer_curve_accel_multiplier;
	uint16_t pointer_curve_accel_divisor;
	uint16_t pointer_curve_max_delta;
	const uint32_t *speed_multipliers;
	const uint32_t *speed_divisors;
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

static int32_t scale_value(struct input_event *event, uint32_t mul, uint32_t div,
			   struct zmk_input_processor_state *state)
{
	if (div == 0) {
		div = 1;
	}

	int64_t value_mul = (int64_t)event->value * (int64_t)mul;

	if (state != NULL && state->remainder != NULL) {
		value_mul += *state->remainder;
	}

	int64_t scaled = value_mul / (int64_t)div;

	if (state != NULL && state->remainder != NULL) {
		*state->remainder = value_mul - (scaled * (int64_t)div);
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
		uint8_t speed_index =
			zmk_pointing_speed_get_index(ZMK_POINTING_SPEED_TARGET_POINTER) %
			cfg->speed_count;
		event->value = scale_value(event, cfg->speed_multipliers[speed_index],
					   cfg->speed_divisors[speed_index], state);
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

	zmk_pointing_speed_set_initial_index(ZMK_POINTING_SPEED_TARGET_POINTER,
					     cfg->initial_speed_index);
	return 0;
}

#define POINTER_SPEED_PROCESSOR_INST(n)                                                            \
	BUILD_ASSERT(DT_INST_PROP_LEN(n, speed_multipliers) ==                                  \
			     DT_INST_PROP_LEN(n, speed_divisors),                                  \
		     "speed-multipliers and speed-divisors must have the same length");             \
	BUILD_ASSERT(DT_INST_PROP_LEN(n, speed_multipliers) > 0,                                 \
		     "at least one speed level is required");                                      \
	static const uint32_t pointer_speed_multipliers_##n[] = DT_INST_PROP(n, speed_multipliers); \
	static const uint32_t pointer_speed_divisors_##n[] = DT_INST_PROP(n, speed_divisors);       \
	static const struct pointer_speed_config pointer_speed_config_##n = {                       \
		.speed_count = DT_INST_PROP_LEN(n, speed_multipliers),                             \
		.initial_speed_index = DT_INST_PROP(n, initial_speed_index),                       \
		.pointer_curve = DT_INST_ENUM_IDX(n, pointer_curve),                               \
		.pointer_curve_deadzone = DT_INST_PROP(n, pointer_curve_deadzone),                 \
		.pointer_curve_accel_multiplier = DT_INST_PROP(n, pointer_curve_accel_multiplier), \
		.pointer_curve_accel_divisor = DT_INST_PROP(n, pointer_curve_accel_divisor),       \
		.pointer_curve_max_delta = DT_INST_PROP(n, pointer_curve_max_delta),               \
		.speed_multipliers = pointer_speed_multipliers_##n,                                \
		.speed_divisors = pointer_speed_divisors_##n,                                      \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, pointer_speed_init, NULL, NULL, &pointer_speed_config_##n,        \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                     \
			      &pointer_speed_driver_api);

DT_INST_FOREACH_STATUS_OKAY(POINTER_SPEED_PROCESSOR_INST)
