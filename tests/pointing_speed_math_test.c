#include <assert.h>
#include <stdint.h>

#include <zmk/pointing_speed_math.h>

static uint32_t q16_percent(uint32_t percent)
{
	return (percent * ZMK_POINTING_SPEED_Q16_SCALE) / 100U;
}

static void assert_near_q16(uint32_t actual, uint32_t expected, uint32_t tolerance)
{
	assert(actual >= expected - tolerance);
	assert(actual <= expected + tolerance);
}

int main(void)
{
	assert(zmk_pointing_speed_multiplier_q16(0, 10, 400) == q16_percent(10));
	assert(zmk_pointing_speed_multiplier_q16(50, 10, 400) == q16_percent(100));
	assert(zmk_pointing_speed_multiplier_q16(100, 10, 400) == q16_percent(400));

	assert_near_q16(zmk_pointing_speed_multiplier_q16(60, 10, 400), q16_percent(132), 900);
	assert_near_q16(zmk_pointing_speed_multiplier_q16(80, 10, 400), q16_percent(230), 1200);

	assert(zmk_pointing_speed_multiplier_q16(100, 10, 1000) == q16_percent(1000));
	assert_near_q16(zmk_pointing_speed_multiplier_q16(60, 10, 1000), q16_percent(158), 1200);
	assert_near_q16(zmk_pointing_speed_multiplier_q16(80, 10, 1000), q16_percent(398), 2000);

	assert_near_q16(zmk_pointing_speed_adjust_multiplier_q16(q16_percent(100), 1, 10, 400),
			q16_percent(101), 1);
	assert_near_q16(zmk_pointing_speed_adjust_multiplier_q16(q16_percent(100), -1, 10, 400),
			q16_percent(99), 1);
	assert(zmk_pointing_speed_adjust_multiplier_q16(q16_percent(10), -1, 10, 400) ==
	       q16_percent(10));
	assert(zmk_pointing_speed_adjust_multiplier_q16(q16_percent(400), 1, 10, 400) ==
	       q16_percent(400));

	return 0;
}
