#pragma once

#include <stdint.h>

enum zmk_pointing_speed_target {
	ZMK_POINTING_SPEED_TARGET_POINTER = 0,
	ZMK_POINTING_SPEED_TARGET_SCROLL = 1,
};

enum zmk_pointing_speed_action {
	ZMK_POINTING_SPEED_ACTION_PREV = 0,
	ZMK_POINTING_SPEED_ACTION_NEXT = 1,
};

uint8_t zmk_pointing_speed_get_index(enum zmk_pointing_speed_target target);
void zmk_pointing_speed_set_initial_index(enum zmk_pointing_speed_target target, uint8_t index);
void zmk_pointing_speed_adjust(enum zmk_pointing_speed_target target,
			       enum zmk_pointing_speed_action action);
