/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_sniping

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>
#include <zmk/pointing_speed.h>
#include <zmk/sniping.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct sniping_config {
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

static int sniping_handle_event(const struct device *dev, struct input_event *event,
				uint32_t param1, uint32_t param2,
				struct zmk_input_processor_state *state)
{
	const struct sniping_config *cfg = dev->config;

	if (event->type != INPUT_EV_REL) {
		return ZMK_INPUT_PROC_CONTINUE;
	}

	switch (event->code) {
	case INPUT_REL_X:
	case INPUT_REL_Y: {
		uint8_t speed_index =
			zmk_pointing_speed_get_index(ZMK_POINTING_SPEED_TARGET_POINTER) %
			cfg->speed_count;
		event->value = scale_value(event, cfg->speed_multipliers[speed_index],
					   cfg->speed_divisors[speed_index], state);
		if (zmk_sniping_is_enabled()) {
			event->value = scale_value(event, param1, param2, state);
		}
		break;
	}
	default:
		break;
	}

	return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api sniping_driver_api = {
	.handle_event = sniping_handle_event,
};

static int sniping_init(const struct device *dev)
{
	const struct sniping_config *cfg = dev->config;

	zmk_pointing_speed_set_initial_index(ZMK_POINTING_SPEED_TARGET_POINTER,
					     cfg->initial_speed_index);
	return 0;
}

#define SNIPING_PROCESSOR_INST(n)                                                                  \
	BUILD_ASSERT(DT_INST_PROP_LEN(n, speed_multipliers) ==                                  \
			     DT_INST_PROP_LEN(n, speed_divisors),                                  \
		     "speed-multipliers and speed-divisors must have the same length");             \
	BUILD_ASSERT(DT_INST_PROP_LEN(n, speed_multipliers) > 0,                                 \
		     "at least one speed level is required");                                      \
	static const uint32_t sniping_speed_multipliers_##n[] = DT_INST_PROP(n, speed_multipliers); \
	static const uint32_t sniping_speed_divisors_##n[] = DT_INST_PROP(n, speed_divisors);       \
	static const struct sniping_config sniping_config_##n = {                                  \
		.speed_count = DT_INST_PROP_LEN(n, speed_multipliers),                             \
		.initial_speed_index = DT_INST_PROP(n, initial_speed_index),                       \
		.speed_multipliers = sniping_speed_multipliers_##n,                                \
		.speed_divisors = sniping_speed_divisors_##n,                                      \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, sniping_init, NULL, NULL, &sniping_config_##n, POST_KERNEL,       \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sniping_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SNIPING_PROCESSOR_INST)
