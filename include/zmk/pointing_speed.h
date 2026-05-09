#pragma once

#include <stdint.h>

enum zmk_pointing_speed_target {
	ZMK_POINTING_SPEED_TARGET_POINTER = 0,
	ZMK_POINTING_SPEED_TARGET_SCROLL = 1,
};

enum zmk_pointing_speed_action {
	ZMK_POINTING_SPEED_ACTION_PREV = 0,
	ZMK_POINTING_SPEED_ACTION_NEXT = 1,
	ZMK_POINTING_SPEED_ACTION_RESET = 2,
	ZMK_POINTING_SPEED_ACTION_FINE_PREV = 3,
	ZMK_POINTING_SPEED_ACTION_FINE_NEXT = 4,
};

uint8_t zmk_pointing_speed_get_position(enum zmk_pointing_speed_target target);
uint32_t zmk_pointing_speed_get_multiplier_q16(enum zmk_pointing_speed_target target);
void zmk_pointing_speed_set_initial_position(enum zmk_pointing_speed_target target,
					     uint8_t position);
void zmk_pointing_speed_set_range(enum zmk_pointing_speed_target target, uint16_t min_percent,
				  uint16_t max_percent);
void zmk_pointing_speed_adjust(enum zmk_pointing_speed_target target,
			       enum zmk_pointing_speed_action action);
