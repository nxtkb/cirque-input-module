#pragma once

#include <stdint.h>

#define ZMK_POINTING_SPEED_MIN_POSITION 0U
#define ZMK_POINTING_SPEED_DEFAULT_POSITION 50U
#define ZMK_POINTING_SPEED_MAX_POSITION 100U
#define ZMK_POINTING_SPEED_Q16_SCALE 65536U

uint32_t zmk_pointing_speed_multiplier_q16(uint8_t position, uint16_t min_percent,
					   uint16_t max_percent);
uint32_t zmk_pointing_speed_adjust_multiplier_q16(uint32_t multiplier_q16, int8_t steps,
						  uint16_t min_percent, uint16_t max_percent);
