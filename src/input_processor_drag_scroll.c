/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_drag_scroll

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>
#include <zmk/drag_scroll.h>
#include <zmk/pointing_speed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define NATIVE_WHEEL_SPEED_BOOST 8

struct drag_scroll_config {
	size_t speed_count;
	uint8_t initial_speed_index;
	const uint32_t *speed_multipliers;
	const uint32_t *speed_divisors;
};

static int16_t scale_value(struct input_event *event, uint32_t mul, uint32_t div,
			   struct zmk_input_processor_state *state)
{
	if (div == 0) {
		div = 1;
	}

	int32_t value_mul = (int32_t)event->value * (int32_t)mul;

	if (state != NULL && state->remainder != NULL) {
		value_mul += *state->remainder;
	}

	int16_t scaled = value_mul / (int32_t)div;

	if (state != NULL && state->remainder != NULL) {
		*state->remainder = value_mul - ((int32_t)scaled * (int32_t)div);
	}

	return scaled;
}

static int drag_scroll_handle_event(const struct device *dev, struct input_event *event,
				    uint32_t param1, uint32_t param2,
				    struct zmk_input_processor_state *state)
{
	const struct drag_scroll_config *cfg = dev->config;

	if (event->type != INPUT_EV_REL) {
		return ZMK_INPUT_PROC_CONTINUE;
	}

	uint8_t speed_index =
		zmk_pointing_speed_get_index(ZMK_POINTING_SPEED_TARGET_SCROLL) % cfg->speed_count;

	switch (event->code) {
	case INPUT_REL_WHEEL:
	case INPUT_REL_HWHEEL:
		event->value = scale_value(event,
					   cfg->speed_multipliers[speed_index] *
						   NATIVE_WHEEL_SPEED_BOOST,
					   cfg->speed_divisors[speed_index], state);
		break;
	case INPUT_REL_X:
		if (!zmk_drag_scroll_is_enabled()) {
			break;
		}
		event->code = INPUT_REL_HWHEEL;
		event->value = scale_value(event, cfg->speed_multipliers[speed_index],
					   cfg->speed_divisors[speed_index], state);
		break;
	case INPUT_REL_Y:
		if (!zmk_drag_scroll_is_enabled()) {
			break;
		}
		event->code = INPUT_REL_WHEEL;
		event->value = scale_value(event, cfg->speed_multipliers[speed_index],
					   cfg->speed_divisors[speed_index], state);
		break;
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
	static const struct drag_scroll_config drag_scroll_config_##n = {                         \
		.speed_count = DT_INST_PROP_LEN(n, speed_multipliers),                             \
		.initial_speed_index = DT_INST_PROP(n, initial_speed_index),                       \
		.speed_multipliers = drag_scroll_speed_multipliers_##n,                            \
		.speed_divisors = drag_scroll_speed_divisors_##n,                                  \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, drag_scroll_init, NULL, NULL, &drag_scroll_config_##n, POST_KERNEL, \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &drag_scroll_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DRAG_SCROLL_PROCESSOR_INST)
