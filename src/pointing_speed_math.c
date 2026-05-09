/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zmk/pointing_speed_math.h>

#define POINTER_MAX_4X_STEP_Q16 67378U
#define SCROLL_MAX_10X_STEP_Q16 68625U

static uint32_t percent_to_q16(uint16_t percent)
{
	return ((uint32_t)percent * ZMK_POINTING_SPEED_Q16_SCALE) / 100U;
}

static uint32_t upper_step_q16(uint16_t max_percent)
{
	switch (max_percent) {
	case 400:
		return POINTER_MAX_4X_STEP_Q16;
	case 1000:
		return SCROLL_MAX_10X_STEP_Q16;
	default:
		return 0;
	}
}

static uint32_t linear_upper_q16(uint8_t position, uint16_t max_percent)
{
	uint32_t steps = position - ZMK_POINTING_SPEED_DEFAULT_POSITION;
	uint64_t weighted_percent =
		(uint64_t)100U * (ZMK_POINTING_SPEED_DEFAULT_POSITION - steps) +
		(uint64_t)max_percent * steps;

	return (weighted_percent * ZMK_POINTING_SPEED_Q16_SCALE) /
	       (100U * ZMK_POINTING_SPEED_DEFAULT_POSITION);
}

uint32_t zmk_pointing_speed_multiplier_q16(uint8_t position, uint16_t min_percent,
					   uint16_t max_percent)
{
	if (position > ZMK_POINTING_SPEED_MAX_POSITION) {
		position = ZMK_POINTING_SPEED_MAX_POSITION;
	}

	if (min_percent > 100U) {
		min_percent = 100U;
	}

	if (max_percent < 100U) {
		max_percent = 100U;
	}

	if (position == ZMK_POINTING_SPEED_DEFAULT_POSITION) {
		return ZMK_POINTING_SPEED_Q16_SCALE;
	}

	if (position == ZMK_POINTING_SPEED_MAX_POSITION) {
		return percent_to_q16(max_percent);
	}

	if (position < ZMK_POINTING_SPEED_DEFAULT_POSITION) {
		uint64_t weighted_percent =
			(uint64_t)min_percent * (ZMK_POINTING_SPEED_DEFAULT_POSITION - position) +
			(uint64_t)100U * position;

		return (weighted_percent * ZMK_POINTING_SPEED_Q16_SCALE) /
		       (100U * ZMK_POINTING_SPEED_DEFAULT_POSITION);
	}

	uint32_t step = upper_step_q16(max_percent);
	if (step == 0) {
		return linear_upper_q16(position, max_percent);
	}

	uint64_t value = ZMK_POINTING_SPEED_Q16_SCALE;
	for (uint8_t i = ZMK_POINTING_SPEED_DEFAULT_POSITION; i < position; i++) {
		value = (value * step + (ZMK_POINTING_SPEED_Q16_SCALE / 2U)) /
			ZMK_POINTING_SPEED_Q16_SCALE;
	}

	return (uint32_t)value;
}

uint32_t zmk_pointing_speed_adjust_multiplier_q16(uint32_t multiplier_q16, int8_t steps,
						  uint16_t min_percent, uint16_t max_percent)
{
	if (min_percent > 100U) {
		min_percent = 100U;
	}

	if (max_percent < 100U) {
		max_percent = 100U;
	}

	uint32_t min_q16 = percent_to_q16(min_percent);
	uint32_t max_q16 = percent_to_q16(max_percent);
	int64_t adjusted = (int64_t)multiplier_q16 +
			   ((int64_t)steps * (int64_t)ZMK_POINTING_SPEED_Q16_SCALE) / 100;

	if (adjusted < min_q16) {
		return min_q16;
	}

	if (adjusted > max_q16) {
		return max_q16;
	}

	return (uint32_t)adjusted;
}
