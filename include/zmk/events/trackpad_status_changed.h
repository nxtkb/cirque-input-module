/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

struct zmk_trackpad_status_changed {
	int64_t timestamp;
};

ZMK_EVENT_DECLARE(zmk_trackpad_status_changed);

static inline int raise_trackpad_status_changed(void)
{
	return raise_zmk_trackpad_status_changed(
		(struct zmk_trackpad_status_changed){.timestamp = k_uptime_get()});
}
