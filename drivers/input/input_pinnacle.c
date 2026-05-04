#define DT_DRV_COMPAT cirque_pinnacle2

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
#include <zephyr/drivers/i2c.h>
#endif
#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)
#include <zephyr/drivers/spi.h>
#endif
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/cirque_mode.h>

#include "input_pinnacle.h"

LOG_MODULE_REGISTER(pinnacle, CONFIG_INPUT_LOG_LEVEL);

static bool pinnacle_is_relative_mode(const struct device *dev)
{
	struct pinnacle_data *drv_data = dev->data;

	return drv_data->relative_mode;
}

static inline bool pinnacle_bus_is_ready(const struct device *dev)
{
	const struct pinnacle_config *config = dev->config;

	return config->bus.is_ready(&config->bus);
}

static inline int pinnacle_write(const struct device *dev, uint8_t address, uint8_t value)
{
	const struct pinnacle_config *config = dev->config;

	return config->bus.write(&config->bus, address, value);
}
static inline int pinnacle_seq_write(const struct device *dev, uint8_t *address, uint8_t *value,
				     uint8_t count)
{
	const struct pinnacle_config *config = dev->config;

	return config->bus.seq_write(&config->bus, address, value, count);
}
static inline int pinnacle_read(const struct device *dev, uint8_t address, uint8_t *value)
{
	const struct pinnacle_config *config = dev->config;

	return config->bus.read(&config->bus, address, value);
}

static inline int pinnacle_seq_read(const struct device *dev, uint8_t address, uint8_t *data,
				    uint8_t count)
{
	const struct pinnacle_config *config = dev->config;

	return config->bus.seq_read(&config->bus, address, data, count);
}

static inline int pinnacle_clear_cmd_complete(const struct device *dev)
{
	const struct pinnacle_config *config = dev->config;

	return config->bus.write(&config->bus, PINNACLE_REG_STATUS1, 0x00);
}

static int pinnacle_read_firmware_id(const struct device *dev, uint8_t *value)
{
	int rc = -EIO;

	for (int i = 0; i < PINNACLE_STARTUP_RETRY_COUNT; i++) {
		rc = pinnacle_read(dev, PINNACLE_REG_FIRMWARE_ID, value);
		if (rc == 0) {
			return 0;
		}

		k_sleep(K_MSEC(PINNACLE_STARTUP_RETRY_DELAY_MS));
	}

	return rc;
}

static int pinnacle_soft_reset(const struct device *dev)
{
	bool ret;
	uint8_t value;
	int rc;

	rc = pinnacle_write(dev, PINNACLE_REG_STATUS1, 0);
	if (rc < 0) {
		LOG_ERR("Failed to clear CC from STATUS1 register (%d)", rc);
		return rc;
	}

	k_sleep(K_MSEC(PINNACLE_STARTUP_RETRY_DELAY_MS));

	/* Datasheet: RESET bit is read-only, reality: write 1 for software reset */
	rc = pinnacle_write(dev, PINNACLE_REG_SYS_CONFIG1, PINNACLE_SYS_CONFIG1_RESET);
	if (rc < 0) {
		LOG_ERR("Failed to write reset to SYS_CONFIG1 (%d)", rc);
		return rc;
	}

	/* Wait until the calibration is completed (SW_CC is asserted) */
	ret = WAIT_FOR(pinnacle_read(dev, PINNACLE_REG_STATUS1, &value) == 0 &&
			       (value & PINNACLE_STATUS1_SW_CC) == PINNACLE_STATUS1_SW_CC,
		       PINNACLE_CALIBRATION_AWAIT_RETRY_COUNT *
			       PINNACLE_CALIBRATION_AWAIT_DELAY_POLL_US,
		       k_sleep(K_USEC(PINNACLE_CALIBRATION_AWAIT_DELAY_POLL_US)));
	if (!ret) {
		LOG_ERR("Failed to wait for calibration completion");
		return -EIO;
	}

	/* Clear SW_CC after reset */
	rc = pinnacle_clear_cmd_complete(dev);
	if (rc) {
		LOG_ERR("Failed to clear SW_CC in Status1");
		return -EIO;
	}

	return 0;
}

static int pinnacle_startup(const struct device *dev, uint8_t *firmware_id)
{
	int rc = -EIO;

	for (int i = 0; i < PINNACLE_INIT_RETRY_COUNT; i++) {
		if (i > 0) {
			k_sleep(K_MSEC(PINNACLE_INIT_RETRY_DELAY_MS));
		}

		rc = pinnacle_soft_reset(dev);
		if (rc) {
			LOG_WRN("Pinnacle soft reset failed on attempt %d/%d (%d)", i + 1,
				PINNACLE_INIT_RETRY_COUNT, rc);
			continue;
		}

		rc = pinnacle_read_firmware_id(dev, firmware_id);
		if (rc) {
			LOG_WRN("Failed to read FirmwareId on attempt %d/%d (%d)", i + 1,
				PINNACLE_INIT_RETRY_COUNT, rc);
			continue;
		}

		if (*firmware_id != PINNACLE_FIRMWARE_ID) {
			LOG_WRN("Incorrect Firmware ASIC ID %x on attempt %d/%d", *firmware_id,
				i + 1, PINNACLE_INIT_RETRY_COUNT);
			rc = -ENODEV;
			continue;
		}

		return 0;
	}

	return rc;
}

static int pinnacle_era_wait_for_completion(const struct device *dev)
{
	bool ret;
	uint8_t value;

	ret = WAIT_FOR(pinnacle_read(dev, PINNACLE_REG_ERA_CTRL, &value) == 0 &&
		       value == PINNACLE_ERA_CTRL_COMPLETE,
		       PINNACLE_ERA_AWAIT_RETRY_COUNT * PINNACLE_ERA_AWAIT_DELAY_POLL_US,
		       k_sleep(K_USEC(PINNACLE_ERA_AWAIT_DELAY_POLL_US)));
	if (!ret) {
		return -EIO;
	}

	return pinnacle_clear_cmd_complete(dev);
}

static int pinnacle_era_write(const struct device *dev, uint16_t address, uint8_t value)
{
	uint8_t address_buf[] = {
		PINNACLE_REG_ERA_VALUE,
		PINNACLE_REG_ERA_ADDR_HIGH,
		PINNACLE_REG_ERA_ADDR_LOW,
		PINNACLE_REG_ERA_CTRL,
	};
	uint8_t value_buf[] = {
		value,
		address >> 8,
		address & 0xFF,
		PINNACLE_ERA_CTRL_WRITE,
	};
	int rc;

	rc = pinnacle_seq_write(dev, address_buf, value_buf, sizeof(address_buf));
	if (rc) {
		return rc;
	}

	return pinnacle_era_wait_for_completion(dev);
}

static int pinnacle_era_read(const struct device *dev, uint16_t address, uint8_t *value)
{
	uint8_t address_buf[] = {
		PINNACLE_REG_ERA_ADDR_HIGH,
		PINNACLE_REG_ERA_ADDR_LOW,
		PINNACLE_REG_ERA_CTRL,
	};
	uint8_t value_buf[] = {
		address >> 8,
		address & 0xFF,
		PINNACLE_ERA_CTRL_READ,
	};
	int rc;

	rc = pinnacle_seq_write(dev, address_buf, value_buf, sizeof(address_buf));
	if (rc) {
		return rc;
	}

	rc = pinnacle_era_wait_for_completion(dev);
	if (rc) {
		return rc;
	}

	return pinnacle_read(dev, PINNACLE_REG_ERA_VALUE, value);
}

static int pinnacle_set_sensitivity(const struct device *dev)
{
	const struct pinnacle_config *config = dev->config;

	uint8_t value;
	int rc;

	rc = pinnacle_era_read(dev, PINNACLE_ERA_REG_CONFIG, &value);
	if (rc) {
		return rc;
	}

	/* Clear BIT(7) and BIT(6) */
	value &= 0x3F;

	switch (config->sensitivity) {
	case PINNACLE_SENSITIVITY_X1:
		value |= PINNACLE_ERA_CONFIG_ADC_ATTENUATION_X1;
		break;
	case PINNACLE_SENSITIVITY_X2:
		value |= PINNACLE_ERA_CONFIG_ADC_ATTENUATION_X2;
		break;
	case PINNACLE_SENSITIVITY_X3:
		value |= PINNACLE_ERA_CONFIG_ADC_ATTENUATION_X3;
		break;
	case PINNACLE_SENSITIVITY_X4:
		value |= PINNACLE_ERA_CONFIG_ADC_ATTENUATION_X4;
		break;
	}

	rc = pinnacle_era_write(dev, PINNACLE_ERA_REG_CONFIG, value);
	if (rc) {
		return rc;
	}

	return 0;
}

static int pinnacle_configure_feed(const struct device *dev, bool relative_mode)
{
	const struct pinnacle_config *config = dev->config;
	uint8_t value;
	int rc;

	if (relative_mode) {
		value = PINNACLE_FEED_CONFIG2_INTELLIMOUSE_ENABLE;
		if (!config->glide_extend_enabled) {
			value |= PINNACLE_FEED_CONFIG2_GLIDE_EXTEND_DISABLE;
		}
		if (config->swap_xy) {
			value |= PINNACLE_FEED_CONFIG2_SWAP_X_AND_Y;
		}
		if (!config->primary_tap_enabled) {
			value |= PINNACLE_FEED_CONFIG2_ALL_TAPS_DISABLE;
		}
	} else {
		value = (PINNACLE_FEED_CONFIG2_GLIDE_EXTEND_DISABLE |
			 PINNACLE_FEED_CONFIG2_SCROLL_DISABLE |
			 PINNACLE_FEED_CONFIG2_ALL_TAPS_DISABLE);
	}
	rc = pinnacle_write(dev, PINNACLE_REG_FEED_CONFIG2, value);
	if (rc) {
		LOG_ERR("Failed to write FeedConfig2");
		return rc;
	}

	value = PINNACLE_FEED_CONFIG1_FEED_ENABLE;
	if (!relative_mode) {
		value |= PINNACLE_FEED_CONFIG1_DATA_MODE_ABSOLUTE;
	}

	/* Absolute-to-relative mode handles inversion in software after differencing. */
	if (relative_mode && config->invert_x) {
		value |= PINNACLE_FEED_CONFIG1_X_INVERT;
	}
	if (relative_mode && config->invert_y) {
		value |= PINNACLE_FEED_CONFIG1_Y_INVERT;
	}

	rc = pinnacle_write(dev, PINNACLE_REG_FEED_CONFIG1, value);
	if (rc) {
		LOG_ERR("Failed to enable Feed in FeedConfig1");
		return rc;
	}

	return 0;
}

static uint8_t pinnacle_idle_packets_count(const struct pinnacle_config *config, bool relative_mode)
{
	uint8_t idle_packets_count = config->idle_packets_count;

	if (((relative_mode && config->primary_tap_enabled) || !relative_mode) &&
	    idle_packets_count == 0) {
		idle_packets_count = 1;
	}

	return idle_packets_count;
}

static int pinnacle_configure_idle_packets(const struct device *dev, bool relative_mode)
{
	const struct pinnacle_config *config = dev->config;
	int rc = pinnacle_write(dev, PINNACLE_REG_Z_IDLE,
				pinnacle_idle_packets_count(config, relative_mode));

	if (rc) {
		LOG_ERR("Failed to set count of Z-idle packets");
	}

	return rc;
}

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
static bool pinnacle_is_ready_i2c(const struct pinnacle_bus *bus)
{
	if (!i2c_is_ready_dt(&bus->i2c)) {
		LOG_ERR("I2C bus %s is not ready", bus->i2c.bus->name);
		return false;
	}

	return true;
}

static int pinnacle_write_i2c(const struct pinnacle_bus *bus, uint8_t address, uint8_t value)
{
	uint8_t buf[] = {PINNACLE_WRITE_REG(address), value};

	return i2c_write_dt(&bus->i2c, buf, 2);
}

static int pinnacle_seq_write_i2c(const struct pinnacle_bus *bus, uint8_t *address, uint8_t *value,
				  uint8_t count)
{
	uint8_t buf[count * 2];

	for (uint8_t i = 0; i < count; ++i) {
		buf[i * 2] = PINNACLE_WRITE_REG(address[i]);
		buf[i * 2 + 1] = value[i];
	}

	return i2c_write_dt(&bus->i2c, buf, count * 2);
}

static int pinnacle_read_i2c(const struct pinnacle_bus *bus, uint8_t address, uint8_t *value)
{
	uint8_t reg = PINNACLE_READ_REG(address);

	return i2c_write_read_dt(&bus->i2c, &reg, 1, value, 1);
}

static int pinnacle_seq_read_i2c(const struct pinnacle_bus *bus, uint8_t address, uint8_t *buf,
				 uint8_t count)
{
	uint8_t reg = PINNACLE_READ_REG(address);

	return i2c_burst_read_dt(&bus->i2c, reg, buf, count);
}
#endif /* DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c) */

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)
static bool pinnacle_is_ready_spi(const struct pinnacle_bus *bus)
{
	if (!spi_is_ready_dt(&bus->spi)) {
		LOG_ERR("SPI bus %s is not ready", bus->spi.bus->name);
		return false;
	}

	return true;
}

static int pinnacle_write_spi(const struct pinnacle_bus *bus, uint8_t address, uint8_t value)
{
	uint8_t tx_data[] = {
		PINNACLE_WRITE_REG(address),
		value,
	};
	const struct spi_buf tx_buf[] = {{
		.buf = tx_data,
		.len = sizeof(tx_data),
	}};
	const struct spi_buf_set tx_set = {
		.buffers = tx_buf,
		.count = ARRAY_SIZE(tx_buf),
	};

	return spi_write_dt(&bus->spi, &tx_set);
}

static int pinnacle_seq_write_spi(const struct pinnacle_bus *bus, uint8_t *address, uint8_t *value,
				  uint8_t count)
{
	uint8_t tx_data[count * 2];
	const struct spi_buf tx_buf[] = {{
		.buf = tx_data,
		.len = sizeof(tx_data),
	}};
	const struct spi_buf_set tx_set = {
		.buffers = tx_buf,
		.count = ARRAY_SIZE(tx_buf),
	};

	for (uint8_t i = 0; i < count; ++i) {
		tx_data[i * 2] = PINNACLE_WRITE_REG(address[i]);
		tx_data[i * 2 + 1] = value[i];
	}

	return spi_write_dt(&bus->spi, &tx_set);
}

static int pinnacle_read_spi(const struct pinnacle_bus *bus, uint8_t address, uint8_t *value)
{
	uint8_t tx_data[] = {
		PINNACLE_READ_REG(address),
		PINNACLE_SPI_FB,
		PINNACLE_SPI_FB,
		PINNACLE_SPI_FB,
	};
	const struct spi_buf tx_buf[] = {{
		.buf = tx_data,
		.len = sizeof(tx_data),
	}};
	const struct spi_buf_set tx_set = {
		.buffers = tx_buf,
		.count = ARRAY_SIZE(tx_buf),
	};

	const struct spi_buf rx_buf[] = {
		{
			.buf = NULL,
			.len = 3,
		},
		{
			.buf = value,
			.len = 1,
		},
	};
	const struct spi_buf_set rx_set = {
		.buffers = rx_buf,
		.count = ARRAY_SIZE(rx_buf),
	};

	int rc;

	rc = spi_transceive_dt(&bus->spi, &tx_set, &rx_set);
	if (rc) {
		LOG_ERR("Failed to read from SPI %s", bus->spi.bus->name);
		return rc;
	}

	return 0;
}

static int pinnacle_seq_read_spi(const struct pinnacle_bus *bus, uint8_t address, uint8_t *buf,
				 uint8_t count)
{

	uint8_t size = count + 3;
	uint8_t tx_data[size];

	tx_data[0] = PINNACLE_READ_REG(address);
	tx_data[1] = PINNACLE_SPI_FC;
	tx_data[2] = PINNACLE_SPI_FC;

	uint8_t i = 3;

	for (; i < (count + 2); ++i) {
		tx_data[i] = PINNACLE_SPI_FC;
	}

	tx_data[i++] = PINNACLE_SPI_FB;

	const struct spi_buf tx_buf[] = {{
		.buf = tx_data,
		.len = size,
	}};
	const struct spi_buf_set tx_set = {
		.buffers = tx_buf,
		.count = 1,
	};

	const struct spi_buf rx_buf[] = {
		{
			.buf = NULL,
			.len = 3,
		},
		{
			.buf = buf,
			.len = count,
		},
	};
	const struct spi_buf_set rx_set = {
		.buffers = rx_buf,
		.count = ARRAY_SIZE(rx_buf),
	};

	int rc;

	rc = spi_transceive_dt(&bus->spi, &tx_set, &rx_set);
	if (rc) {
		LOG_ERR("Failed to read from SPI %s", bus->spi.bus->name);
		return rc;
	}

	return 0;
}
#endif /* DT_ANY_INST_ON_BUS_STATUS_OKAY(spi) */

static void pinnacle_decode_sample(const struct device *dev, uint8_t *rx,
				   union pinnacle_sample *sample)
{
	const struct pinnacle_config *config = dev->config;

	if (pinnacle_is_relative_mode(dev)) {
		if (config->primary_tap_enabled) {
			sample->btn_primary = (rx[0] & PINNACLE_PACKET_BYTE0_BTN_PRIMARY) == PINNACLE_PACKET_BYTE0_BTN_PRIMARY;
			sample->btn_secondary = (rx[0] & PINNACLE_PACKET_BYTE0_BTN_SECONDRY) == PINNACLE_PACKET_BYTE0_BTN_SECONDRY;
			sample->btn_aux = (rx[0] & PINNACLE_PACKET_BYTE0_BTN_AUX) == PINNACLE_PACKET_BYTE0_BTN_AUX;
		}
		sample->rel_x = ((rx[0] & PINNACLE_PACKET_BYTE0_X_SIGN) == PINNACLE_PACKET_BYTE0_X_SIGN)
					? -(256 - rx[1])
					: rx[1];
		sample->rel_y = ((rx[0] & PINNACLE_PACKET_BYTE0_Y_SIGN) == PINNACLE_PACKET_BYTE0_Y_SIGN)
					? -(256 - rx[2])
					: rx[2];
		sample->wheelCount = (rx[3] & 0x80) ? -(256 - rx[3]) : rx[3];
	} else {
		sample->abs_x = ((rx[2] & 0x0F) << 8) | rx[0];
		sample->abs_y = ((rx[2] & 0xF0) << 4) | rx[1];
		sample->abs_z = rx[3] & 0x3F;
	}
}

static bool pinnacle_is_idle_sample(const union pinnacle_sample *sample)
{
	return (sample->abs_x == 0 && sample->abs_y == 0 && sample->abs_z == 0);
}

static bool pinnacle_is_absolute_touch_sample(const struct device *dev,
					      const union pinnacle_sample *sample)
{
	const struct pinnacle_config *config = dev->config;

	return sample->abs_z >= config->absolute_touch_min_z;
}

static void pinnacle_clip_sample(const struct device *dev, union pinnacle_sample *sample)
{
	const struct pinnacle_config *config = dev->config;

	if (sample->abs_x < config->active_range_x_min) {
		sample->abs_x = config->active_range_x_min;
	}
	if (sample->abs_x > config->active_range_x_max) {
		sample->abs_x = config->active_range_x_max;
	}
	if (sample->abs_y < config->active_range_y_min) {
		sample->abs_y = config->active_range_y_min;
	}
	if (sample->abs_y > config->active_range_y_max) {
		sample->abs_y = config->active_range_y_max;
	}
}

static void pinnacle_scale_sample(const struct device *dev, union pinnacle_sample *sample)
{
	const struct pinnacle_config *config = dev->config;

	uint16_t range_x = config->active_range_x_max - config->active_range_x_min;
	uint16_t range_y = config->active_range_y_max - config->active_range_y_min;

	sample->abs_x = (uint16_t)((uint32_t)(sample->abs_x - config->active_range_x_min) *
				   config->resolution_x / range_x);
	sample->abs_y = (uint16_t)((uint32_t)(sample->abs_y - config->active_range_y_min) *
				   config->resolution_y / range_y);
}

static int16_t pinnacle_scale_absolute_delta(const struct device *dev, int32_t delta)
{
	const struct pinnacle_config *config = dev->config;
	int64_t scaled = (int64_t)delta * config->absolute_relative_multiplier;

	scaled /= config->absolute_relative_divisor;

	return CLAMP(scaled, INT16_MIN, INT16_MAX);
}

static void pinnacle_apply_absolute_transform(const struct pinnacle_config *config,
					    int32_t *delta_x, int32_t *delta_y)
{
	if (config->swap_xy) {
		int32_t tmp = *delta_x;

		*delta_x = *delta_y;
		*delta_y = tmp;
	}

	if (config->invert_x) {
		*delta_x = -*delta_x;
	}
}

static int32_t pinnacle_abs32(int32_t value)
{
	return value < 0 ? -value : value;
}

static uint16_t pinnacle_abs_left(const struct pinnacle_config *config)
{
	return config->active_range_x_min;
}

static uint16_t pinnacle_abs_right(const struct pinnacle_config *config)
{
	return config->active_range_x_max;
}

static uint16_t pinnacle_abs_top(const struct pinnacle_config *config)
{
	return config->active_range_y_min;
}

static uint16_t pinnacle_abs_bottom(const struct pinnacle_config *config)
{
	return config->active_range_y_max;
}

static bool pinnacle_absolute_point_in_lower_right(const struct pinnacle_config *config,
						  uint16_t x, uint16_t y)
{
	if (config->absolute_secondary_tap_area_width == 0 ||
	    config->absolute_secondary_tap_area_height == 0) {
		return false;
	}

	int32_t right = pinnacle_abs_right(config);
	int32_t left = pinnacle_abs_left(config);
	int32_t top = pinnacle_abs_top(config);
	int32_t bottom = pinnacle_abs_bottom(config);
	int32_t left_boundary = MAX(left, right - config->absolute_secondary_tap_area_width);
	int32_t top_boundary = MAX(top, bottom - config->absolute_secondary_tap_area_height);

	return x >= left_boundary && y >= top_boundary;
}

static bool pinnacle_absolute_point_in_upper_left(const struct pinnacle_config *config,
						 uint16_t x, uint16_t y)
{
	if (config->absolute_aux_tap_area_width == 0 ||
	    config->absolute_aux_tap_area_height == 0) {
		return false;
	}

	int32_t right = pinnacle_abs_right(config);
	int32_t left = pinnacle_abs_left(config);
	int32_t top = pinnacle_abs_top(config);
	int32_t bottom = pinnacle_abs_bottom(config);
	int32_t right_boundary = MIN(right, left + config->absolute_aux_tap_area_width);
	int32_t bottom_boundary = MIN(bottom, top + config->absolute_aux_tap_area_height);

	return x <= right_boundary && y <= bottom_boundary;
}

static void pinnacle_absolute_logical_position(const struct pinnacle_config *config,
					      uint16_t raw_x, uint16_t raw_y,
					      int32_t *logical_x, int32_t *logical_y,
					      int32_t *logical_width, int32_t *logical_height)
{
	int32_t left = config->active_range_x_min;
	int32_t right = config->active_range_x_max;
	int32_t top = config->active_range_y_min;
	int32_t bottom = config->active_range_y_max;
	int32_t x = CLAMP((int32_t)raw_x, left, right) - left;
	int32_t y = CLAMP((int32_t)raw_y, top, bottom) - top;
	int32_t width = right - left;
	int32_t height = bottom - top;

	if (config->swap_xy) {
		int32_t tmp = x;

		x = y;
		y = tmp;
		tmp = width;
		width = height;
		height = tmp;
	}

	if (config->invert_x) {
		x = width - x;
	}

	*logical_x = x;
	*logical_y = y;
	*logical_width = width;
	*logical_height = height;
}

static bool pinnacle_absolute_edge_motion_active(const struct device *dev, int64_t *norm_x,
						 int64_t *norm_y)
{
	const struct pinnacle_config *config = dev->config;
	struct pinnacle_data *drv_data = dev->data;
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	int32_t zone = config->absolute_edge_motion_zone;
	int64_t radius_x;
	int64_t radius_y;
	int64_t radius_min;
	int64_t distance_sq;
	int64_t threshold;

	if (!config->absolute_edge_motion_enabled || zone == 0) {
		return false;
	}

	pinnacle_absolute_logical_position(config, drv_data->touch_current_x,
						 drv_data->touch_current_y, &x, &y, &width, &height);
	radius_x = width / 2;
	radius_y = height / 2;
	radius_min = MIN(radius_x, radius_y);

	if (radius_x == 0 || radius_y == 0 || radius_min == 0) {
		return false;
	}

	/*
	 * Normalizing both axes makes the edge threshold round/oval instead of a set
	 * of rectangular bands.
	 */
	*norm_x = ((int64_t)x * 2 - width) * 1024 / radius_x;
	*norm_y = ((int64_t)y * 2 - height) * 1024 / radius_y;
	distance_sq = *norm_x * *norm_x + *norm_y * *norm_y;
	threshold = 2048 - ((int64_t)MIN(zone, radius_min) * 2048 / radius_min);

	return distance_sq >= threshold * threshold;
}

static void pinnacle_absolute_edge_motion_delta_from_vector(const struct pinnacle_config *config,
							    int64_t vector_x,
							    int64_t vector_y,
							    int32_t *delta_x,
							    int32_t *delta_y)
{
	int64_t abs_x = vector_x < 0 ? -vector_x : vector_x;
	int64_t abs_y = vector_y < 0 ? -vector_y : vector_y;
	int32_t speed = config->absolute_edge_motion_speed;
	int32_t diagonal = MAX(1, (speed * 3) / 4);
	int32_t sign_x = vector_x < 0 ? -1 : 1;
	int32_t sign_y = vector_y < 0 ? -1 : 1;

	if (abs_x == 0 && abs_y == 0) {
		return;
	}

	/*
	 * Quantize edge motion into eight stable sectors. The thresholds are
	 * tan(22.5 deg) and tan(67.5 deg), scaled by 1000.
	 */
	if (abs_y * 1000 <= abs_x * 414) {
		*delta_x = sign_x * speed;
		*delta_y = 0;
	} else if (abs_x * 1000 <= abs_y * 414) {
		*delta_x = 0;
		*delta_y = sign_y * speed;
	} else {
		*delta_x = sign_x * diagonal;
		*delta_y = sign_y * diagonal;
	}
}

static void pinnacle_absolute_edge_motion_delta(const struct device *dev, int32_t *delta_x,
					       int32_t *delta_y)
{
	const struct pinnacle_config *config = dev->config;
	int64_t norm_x;
	int64_t norm_y;

	*delta_x = 0;
	*delta_y = 0;

	if (!pinnacle_absolute_edge_motion_active(dev, &norm_x, &norm_y)) {
		return;
	}

	pinnacle_absolute_edge_motion_delta_from_vector(config, norm_x, norm_y, delta_x, delta_y);
}

static enum pinnacle_absolute_scroll_mode
pinnacle_absolute_scroll_mode_for_touch(const struct device *dev, uint16_t raw_x, uint16_t raw_y)
{
	const struct pinnacle_config *config = dev->config;
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	int32_t zone = config->absolute_scroll_zone;
	int32_t zone_x;
	int32_t zone_y;

	if ((!config->absolute_right_edge_scroll_enabled &&
	     !config->absolute_top_edge_scroll_enabled) || zone == 0) {
		return PINNACLE_ABSOLUTE_SCROLL_NONE;
	}

	pinnacle_absolute_logical_position(config, raw_x, raw_y, &x, &y, &width, &height);
	zone_x = MIN(zone, width);
	zone_y = MIN(zone, height);

	if (config->absolute_right_edge_scroll_enabled && x >= width - zone_x) {
		return PINNACLE_ABSOLUTE_SCROLL_VERTICAL;
	}

	if (config->absolute_top_edge_scroll_enabled && y <= zone_y) {
		return PINNACLE_ABSOLUTE_SCROLL_HORIZONTAL;
	}

	return PINNACLE_ABSOLUTE_SCROLL_NONE;
}

static int16_t pinnacle_absolute_scroll_accumulate(struct pinnacle_data *drv_data,
						  int32_t delta, uint16_t divisor)
{
	int32_t ticks;

	drv_data->scroll_remainder += delta;
	ticks = drv_data->scroll_remainder / divisor;
	drv_data->scroll_remainder %= divisor;

	return CLAMP(ticks, INT16_MIN, INT16_MAX);
}

static void pinnacle_report_absolute_scroll(const struct device *dev, uint16_t previous_raw_x,
					   uint16_t previous_raw_y, uint16_t current_raw_x,
					   uint16_t current_raw_y)
{
	const struct pinnacle_config *config = dev->config;
	struct pinnacle_data *drv_data = dev->data;
	int32_t previous_x;
	int32_t previous_y;
	int32_t current_x;
	int32_t current_y;
	int32_t width;
	int32_t height;
	int32_t delta;
	int16_t ticks;
	uint16_t code;

	if (drv_data->scroll_mode == PINNACLE_ABSOLUTE_SCROLL_NONE) {
		return;
	}

	pinnacle_absolute_logical_position(config, previous_raw_x, previous_raw_y, &previous_x,
					 &previous_y, &width, &height);
	pinnacle_absolute_logical_position(config, current_raw_x, current_raw_y, &current_x,
					 &current_y, &width, &height);

	if (drv_data->scroll_mode == PINNACLE_ABSOLUTE_SCROLL_VERTICAL) {
		delta = previous_y - current_y;
		code = INPUT_REL_WHEEL;
	} else {
		delta = current_x - previous_x;
		code = INPUT_REL_HWHEEL;
	}

	ticks = pinnacle_absolute_scroll_accumulate(drv_data, delta,
						      config->absolute_scroll_divisor);
	if (ticks != 0) {
		input_report_rel(dev, code, ticks, true, K_FOREVER);
	}
}

static void pinnacle_schedule_absolute_edge_motion(const struct device *dev)
{
	const struct pinnacle_config *config = dev->config;
	struct pinnacle_data *drv_data = dev->data;
	int32_t delta_x;
	int32_t delta_y;
	int64_t elapsed_ms;
	int64_t delay_ms;

	if (pinnacle_is_relative_mode(dev) || !drv_data->touching ||
	    !config->absolute_edge_motion_enabled ||
	    drv_data->scroll_mode != PINNACLE_ABSOLUTE_SCROLL_NONE) {
		k_work_cancel_delayable(&drv_data->edge_motion_work);
		return;
	}

	pinnacle_absolute_edge_motion_delta(dev, &delta_x, &delta_y);
	if (delta_x == 0 && delta_y == 0) {
		k_work_cancel_delayable(&drv_data->edge_motion_work);
		return;
	}

	elapsed_ms = k_uptime_get() - drv_data->touch_start_time_ms;
	delay_ms = MAX(0, (int64_t)config->absolute_edge_motion_start_ms - elapsed_ms);
	if (delay_ms == 0) {
		delay_ms = config->absolute_edge_motion_interval_ms;
	}

	k_work_schedule(&drv_data->edge_motion_work, K_MSEC(delay_ms));
}

static void pinnacle_edge_motion_work_cb(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct pinnacle_data *drv_data = CONTAINER_OF(dwork, struct pinnacle_data, edge_motion_work);
	const struct device *dev = drv_data->dev;
	const struct pinnacle_config *config = dev->config;
	int32_t delta_x;
	int32_t delta_y;

	if (pinnacle_is_relative_mode(dev) || !drv_data->touching ||
	    drv_data->scroll_mode != PINNACLE_ABSOLUTE_SCROLL_NONE) {
		return;
	}

	pinnacle_absolute_edge_motion_delta(dev, &delta_x, &delta_y);
	if (delta_x == 0 && delta_y == 0) {
		return;
	}

	input_report_rel(dev, INPUT_REL_X, CLAMP(delta_x, INT16_MIN, INT16_MAX), false, K_FOREVER);
	input_report_rel(dev, INPUT_REL_Y, CLAMP(delta_y, INT16_MIN, INT16_MAX), true, K_FOREVER);
	k_work_reschedule(&drv_data->edge_motion_work,
			  K_MSEC(config->absolute_edge_motion_interval_ms));
}

static uint16_t pinnacle_absolute_tap_code(const struct device *dev, uint16_t x, uint16_t y)
{
	const struct pinnacle_config *config = dev->config;

	if (pinnacle_absolute_point_in_lower_right(config, x, y)) {
		return INPUT_BTN_0 + 1;
	}

	if (pinnacle_absolute_point_in_upper_left(config, x, y)) {
		return INPUT_BTN_0 + 2;
	}

	return INPUT_BTN_0;
}

static bool pinnacle_should_start_absolute_tap_drag(const struct device *dev, uint16_t x,
						   uint16_t y)
{
	const struct pinnacle_config *config = dev->config;
	struct pinnacle_data *drv_data = dev->data;
	int64_t elapsed_ms = k_uptime_get() - drv_data->last_tap_time_ms;
	int32_t movement_x = pinnacle_abs32((int32_t)x - (int32_t)drv_data->last_tap_x);
	int32_t movement_y = pinnacle_abs32((int32_t)y - (int32_t)drv_data->last_tap_y);

	return config->absolute_tap_drag_enabled && drv_data->last_tap_time_ms > 0 &&
	       drv_data->last_tap_code == INPUT_BTN_0 &&
	       elapsed_ms <= config->absolute_tap_drag_timeout_ms &&
	       movement_x <= config->absolute_tap_drag_max_movement &&
	       movement_y <= config->absolute_tap_drag_max_movement;
}

static void pinnacle_handle_absolute_release(const struct device *dev)
{
	const struct pinnacle_config *config = dev->config;
	struct pinnacle_data *drv_data = dev->data;

	if (!drv_data->touching) {
		return;
	}

	k_work_cancel_delayable(&drv_data->edge_motion_work);

	if (drv_data->tap_dragging) {
		input_report_key(dev, INPUT_BTN_0, false, true, K_FOREVER);
		drv_data->tap_dragging = false;
		drv_data->touching = false;
		drv_data->scroll_mode = PINNACLE_ABSOLUTE_SCROLL_NONE;
		drv_data->scroll_remainder = 0;
		drv_data->last_tap_time_ms = 0;
		return;
	}

	drv_data->scroll_mode = PINNACLE_ABSOLUTE_SCROLL_NONE;
	drv_data->scroll_remainder = 0;

	int64_t duration_ms = k_uptime_get() - drv_data->touch_start_time_ms;
	int32_t movement_x = pinnacle_abs32((int32_t)drv_data->previous_abs_x -
					     (int32_t)drv_data->touch_start_abs_x);
	int32_t movement_y = pinnacle_abs32((int32_t)drv_data->previous_abs_y -
					     (int32_t)drv_data->touch_start_abs_y);

	if (config->primary_tap_enabled && duration_ms <= config->absolute_tap_max_ms &&
	    movement_x <= config->absolute_tap_max_movement &&
	    movement_y <= config->absolute_tap_max_movement) {
		uint16_t code = pinnacle_absolute_tap_code(dev, drv_data->touch_current_x,
								 drv_data->touch_current_y);
		input_report_key(dev, code, true, true, K_FOREVER);
		k_sleep(K_MSEC(config->absolute_tap_click_ms));
		input_report_key(dev, code, false, true, K_FOREVER);

		drv_data->last_tap_code = code;
		if (code == INPUT_BTN_0) {
			drv_data->last_tap_time_ms = k_uptime_get();
			drv_data->last_tap_x = drv_data->touch_current_x;
			drv_data->last_tap_y = drv_data->touch_current_y;
		} else {
			drv_data->last_tap_time_ms = 0;
		}
	}

	drv_data->touching = false;
	drv_data->scroll_mode = PINNACLE_ABSOLUTE_SCROLL_NONE;
	drv_data->scroll_remainder = 0;
}

static void pinnacle_clear_runtime_state(const struct device *dev)
{
	struct pinnacle_data *drv_data = dev->data;

	k_work_cancel_delayable(&drv_data->edge_motion_work);

	if (drv_data->tap_dragging || drv_data->btn_primary) {
		input_report_key(dev, INPUT_BTN_0, false, true, K_FOREVER);
	}
	if (drv_data->btn_secondary) {
		input_report_key(dev, INPUT_BTN_0 + 1, false, true, K_FOREVER);
	}
	if (drv_data->btn_aux) {
		input_report_key(dev, INPUT_BTN_0 + 2, false, true, K_FOREVER);
	}

	drv_data->btn_primary = false;
	drv_data->btn_secondary = false;
	drv_data->btn_aux = false;
	drv_data->touching = false;
	drv_data->tap_dragging = false;
	drv_data->scroll_mode = PINNACLE_ABSOLUTE_SCROLL_NONE;
	drv_data->scroll_remainder = 0;
	drv_data->last_tap_time_ms = 0;
	drv_data->last_tap_code = 0;
}

bool zmk_cirque_mode_is_relative(const struct device *dev)
{
	return pinnacle_is_relative_mode(dev);
}

int zmk_cirque_mode_apply(const struct device *dev, enum zmk_cirque_mode_action action)
{
	struct pinnacle_data *drv_data = dev->data;
	bool relative_mode;
	int rc;

	switch (action) {
	case ZMK_CIRQUE_MODE_ACTION_RELATIVE:
		relative_mode = true;
		break;
	case ZMK_CIRQUE_MODE_ACTION_ABSOLUTE:
		relative_mode = false;
		break;
	case ZMK_CIRQUE_MODE_ACTION_TOGGLE:
		relative_mode = !drv_data->relative_mode;
		break;
	default:
		return -EINVAL;
	}

	if (relative_mode == drv_data->relative_mode) {
		return 0;
	}

	pinnacle_clear_runtime_state(dev);

	rc = pinnacle_configure_feed(dev, relative_mode);
	if (rc) {
		return rc;
	}
	drv_data->relative_mode = relative_mode;

	rc = pinnacle_configure_idle_packets(dev, relative_mode);
	if (rc) {
		return rc;
	}

	rc = pinnacle_write(dev, PINNACLE_REG_STATUS1, 0x00);
	if (rc) {
		LOG_ERR("Failed to clear SW_CC and SW_DR");
		return rc;
	}

	zmk_cirque_mode_report(dev, relative_mode);

	LOG_INF("Cirque data mode switched to %s", relative_mode ? "relative" : "absolute");

	return 0;
}

static int pinnacle_sample_fetch(const struct device *dev, union pinnacle_sample *sample)
{
	uint8_t rx[4];
	int rc;

	if (pinnacle_is_relative_mode(dev)) {
		rc = pinnacle_seq_read(dev, PINNACLE_REG_PACKET_BYTE0, rx, 4);
	} else {
		rc = pinnacle_seq_read(dev, PINNACLE_REG_PACKET_BYTE2, rx, 4);
	}

	if (rc) {
		LOG_ERR("Failed to read data from SPI device");
		return rc;
	}

	pinnacle_decode_sample(dev, rx, sample);

	rc = pinnacle_write(dev, PINNACLE_REG_STATUS1, 0x00);
	if (rc) {
		LOG_ERR("Failed to clear SW_CC and SW_DR");
		return rc;
	}

	return 0;
}

static bool pinnacle_report_button_if_changed(const struct device *dev, uint16_t code,
					      bool current, bool *previous, bool sync)
{
	if (current == *previous) {
		return false;
	}

	*previous = current;
	input_report_key(dev, code, current, sync, K_FOREVER);
	return true;
}

static bool pinnacle_buttons_changed(const struct pinnacle_data *data,
				     const union pinnacle_sample *sample)
{
	return sample->btn_primary != data->btn_primary ||
	       sample->btn_secondary != data->btn_secondary ||
	       sample->btn_aux != data->btn_aux;
}

static int pinnacle_handle_interrupt(const struct device *dev)
{
	const struct pinnacle_config *config = dev->config;
	struct pinnacle_data *drv_data = dev->data;
	union pinnacle_sample *sample = &drv_data->sample;

	int rc;

	rc = pinnacle_sample_fetch(dev, sample);
	if (rc) {
		LOG_ERR("Failed to read data packets");
		return rc;
	}

	if (pinnacle_is_relative_mode(dev)) {
		bool buttons_changed =
			config->primary_tap_enabled && pinnacle_buttons_changed(drv_data, sample);

		if (sample->wheelCount != 0) {
			input_report_rel(dev, INPUT_REL_WHEEL, sample->wheelCount, true, K_FOREVER);
		} else {
			input_report_rel(dev, INPUT_REL_X, sample->rel_x, false, K_FOREVER);
			input_report_rel(dev, INPUT_REL_Y, sample->rel_y, !buttons_changed, K_FOREVER);
		}
		if (config->primary_tap_enabled) {
			bool secondary_changed = sample->btn_secondary != drv_data->btn_secondary;

			pinnacle_report_button_if_changed(dev, INPUT_BTN_0, sample->btn_primary,
							  &drv_data->btn_primary,
							  buttons_changed && !secondary_changed &&
								  sample->btn_aux == drv_data->btn_aux);
			pinnacle_report_button_if_changed(dev, INPUT_BTN_0 + 1,
							  sample->btn_secondary,
							  &drv_data->btn_secondary,
							  buttons_changed &&
								  sample->btn_aux == drv_data->btn_aux);
			pinnacle_report_button_if_changed(dev, INPUT_BTN_0 + 2, sample->btn_aux,
							  &drv_data->btn_aux, buttons_changed);
		}
	} else {
		if (pinnacle_is_idle_sample(sample) || !pinnacle_is_absolute_touch_sample(dev, sample)) {
			pinnacle_handle_absolute_release(dev);
			return 0;
		}

		if (config->clipping_enabled) {
			pinnacle_clip_sample(dev, sample);
			if (config->scaling_enabled) {
				pinnacle_scale_sample(dev, sample);
			}
		}

		if (!drv_data->touching) {
			if (drv_data->last_tap_time_ms > 0 &&
			    k_uptime_get() - drv_data->last_tap_time_ms >
				    config->absolute_tap_drag_timeout_ms) {
				drv_data->last_tap_time_ms = 0;
			}

			drv_data->touching = true;
			drv_data->tap_dragging =
				pinnacle_should_start_absolute_tap_drag(dev, sample->abs_x, sample->abs_y);
			if (drv_data->tap_dragging) {
				input_report_key(dev, INPUT_BTN_0, true, true, K_FOREVER);
				drv_data->last_tap_time_ms = 0;
				drv_data->scroll_mode = PINNACLE_ABSOLUTE_SCROLL_NONE;
			} else {
				drv_data->scroll_mode =
					pinnacle_absolute_scroll_mode_for_touch(dev, sample->abs_x,
									     sample->abs_y);
			}
			drv_data->scroll_remainder = 0;

			drv_data->previous_abs_x = sample->abs_x;
			drv_data->previous_abs_y = sample->abs_y;
			drv_data->touch_start_abs_x = sample->abs_x;
			drv_data->touch_start_abs_y = sample->abs_y;
			drv_data->touch_current_x = sample->abs_x;
			drv_data->touch_current_y = sample->abs_y;
			drv_data->touch_start_time_ms = k_uptime_get();
			pinnacle_schedule_absolute_edge_motion(dev);
			return 0;
		}

		if (drv_data->scroll_mode != PINNACLE_ABSOLUTE_SCROLL_NONE) {
			pinnacle_report_absolute_scroll(dev, drv_data->previous_abs_x,
								drv_data->previous_abs_y, sample->abs_x,
								sample->abs_y);
			drv_data->previous_abs_x = sample->abs_x;
			drv_data->previous_abs_y = sample->abs_y;
			drv_data->touch_current_x = sample->abs_x;
			drv_data->touch_current_y = sample->abs_y;
			return 0;
		}

		int32_t delta_x = (int32_t)sample->abs_x - (int32_t)drv_data->previous_abs_x;
		int32_t delta_y = (int32_t)sample->abs_y - (int32_t)drv_data->previous_abs_y;

		pinnacle_apply_absolute_transform(config, &delta_x, &delta_y);
		int16_t rel_x = pinnacle_scale_absolute_delta(dev, delta_x);
		int16_t rel_y = pinnacle_scale_absolute_delta(dev, delta_y);

		drv_data->previous_abs_x = sample->abs_x;
		drv_data->previous_abs_y = sample->abs_y;
		drv_data->touch_current_x = sample->abs_x;
		drv_data->touch_current_y = sample->abs_y;
		pinnacle_schedule_absolute_edge_motion(dev);

		input_report_rel(dev, INPUT_REL_X, rel_x, false, K_FOREVER);
		input_report_rel(dev, INPUT_REL_Y, rel_y, true, K_FOREVER);
	}

	return 0;
}

static void pinnacle_data_ready_gpio_callback(const struct device *dev, struct gpio_callback *cb,
					      uint32_t pins)
{
	struct pinnacle_data *drv_data = CONTAINER_OF(cb, struct pinnacle_data, dr_cb_data);

	k_work_submit(&drv_data->work);
}

static void pinnacle_work_cb(struct k_work *work)
{
	struct pinnacle_data *drv_data = CONTAINER_OF(work, struct pinnacle_data, work);

	pinnacle_handle_interrupt(drv_data->dev);
}

int pinnacle_init_interrupt(const struct device *dev)
{
	struct pinnacle_data *drv_data = dev->data;
	const struct pinnacle_config *config = dev->config;
	const struct gpio_dt_spec *gpio = &config->dr_gpio;

	int rc;

	drv_data->dev = dev;
	drv_data->work.handler = pinnacle_work_cb;
	k_work_init_delayable(&drv_data->edge_motion_work, pinnacle_edge_motion_work_cb);

	/* Configure GPIO pin for HW_DR signal */
	rc = gpio_is_ready_dt(gpio);
	if (!rc) {
		LOG_ERR("GPIO device %s/%d is not ready", gpio->port->name, gpio->pin);
		return -ENODEV;
	}

	rc = gpio_pin_configure_dt(gpio, GPIO_INPUT);
	if (rc) {
		LOG_ERR("Failed to configure %s/%d as input", gpio->port->name, gpio->pin);
		return rc;
	}

	rc = gpio_pin_interrupt_configure_dt(gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc) {
		LOG_ERR("Failed to configured interrupt for %s/%d", gpio->port->name, gpio->pin);
		return rc;
	}

	gpio_init_callback(&drv_data->dr_cb_data, pinnacle_data_ready_gpio_callback,
			   BIT(gpio->pin));

	rc = gpio_add_callback(gpio->port, &drv_data->dr_cb_data);
	if (rc) {
		LOG_ERR("Failed to configured interrupt for %s/%d", gpio->port->name, gpio->pin);
		return rc;
	}

	return 0;
}

static int pinnacle_init(const struct device *dev)
{
	const struct pinnacle_config *config = dev->config;
	struct pinnacle_data *drv_data = dev->data;

	int rc;
	uint8_t value;

	if (!pinnacle_bus_is_ready(dev)) {
		return -ENODEV;
	}

	if (config->startup_delay_ms > 0) {
		k_sleep(K_MSEC(config->startup_delay_ms));
	}

	rc = pinnacle_startup(dev, &value);
	if (rc) {
		LOG_ERR("Pinnacle startup failed (%d)", rc);
		return rc;
	}

	/* Set trackpad sensitivity */
	rc = pinnacle_set_sensitivity(dev);
	if (rc) {
		LOG_ERR("Failed to set sensitivity");
		return -EIO;
	}

	value = 0x00;
	if (config->sleep_mode_enable) {
		value |= PINNACLE_SYS_CONFIG1_LOW_POWER_MODE;
	}

	rc = pinnacle_write(dev, PINNACLE_REG_SYS_CONFIG1, value);
	if (rc) {
		LOG_ERR("Failed to write SysConfig1");
		return rc;
	}

	drv_data->relative_mode = config->relative_mode;

	rc = pinnacle_configure_feed(dev, drv_data->relative_mode);
	if (rc) {
		return rc;
	}

	rc = pinnacle_configure_idle_packets(dev, drv_data->relative_mode);
	if (rc) {
		return rc;
	}

	zmk_cirque_mode_report(dev, drv_data->relative_mode);

	rc = pinnacle_init_interrupt(dev);
	if (rc) {
		LOG_ERR("Failed to initialize interrupts");
		return rc;
	}

	return 0;
}

#define PINNACLE_CONFIG_BUS_I2C(inst)                                                              \
	.bus = {                                                                                   \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.is_ready = pinnacle_is_ready_i2c,                                                 \
		.write = pinnacle_write_i2c,                                                       \
		.seq_write = pinnacle_seq_write_i2c,                                               \
		.read = pinnacle_read_i2c,                                                         \
		.seq_read = pinnacle_seq_read_i2c,                                                 \
	}

#define PINNACLE_SPI_OP (SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_MODE_CPHA | SPI_WORD_SET(8))
#define PINNACLE_CONFIG_BUS_SPI(inst)                                                              \
	.bus = {                                                                                   \
		.spi = SPI_DT_SPEC_INST_GET(inst, PINNACLE_SPI_OP, 0),                                \
		.is_ready = pinnacle_is_ready_spi,                                                 \
		.write = pinnacle_write_spi,                                                       \
		.seq_write = pinnacle_seq_write_spi,                                               \
		.read = pinnacle_read_spi,                                                         \
		.seq_read = pinnacle_seq_read_spi,                                                 \
	}

#define PINNACLE_DEFINE(inst)                                                                      \
	static const struct pinnacle_config pinnacle_config_##inst = {                             \
		COND_CODE_1(DT_INST_ON_BUS(inst, i2c), (PINNACLE_CONFIG_BUS_I2C(inst),), ())       \
		COND_CODE_1(DT_INST_ON_BUS(inst, spi), (PINNACLE_CONFIG_BUS_SPI(inst),), ())       \
		.dr_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, data_ready_gpios, {}),                   \
		.relative_mode = DT_INST_ENUM_IDX(inst, data_mode),                                \
		.sensitivity = DT_INST_ENUM_IDX(inst, sensitivity),                                \
		.idle_packets_count = DT_INST_PROP(inst, idle_packets_count),                      \
		.startup_delay_ms = DT_INST_PROP(inst, startup_delay_ms),                          \
		.absolute_relative_multiplier = DT_INST_PROP(inst, absolute_relative_multiplier),   \
		.absolute_relative_divisor = DT_INST_PROP(inst, absolute_relative_divisor),         \
		.absolute_touch_min_z = DT_INST_PROP(inst, absolute_touch_min_z),                  \
		.absolute_tap_max_ms = DT_INST_PROP(inst, absolute_tap_max_ms),                     \
		.absolute_tap_max_movement = DT_INST_PROP(inst, absolute_tap_max_movement),         \
		.absolute_tap_click_ms = DT_INST_PROP(inst, absolute_tap_click_ms),                 \
		.absolute_tap_drag_timeout_ms = DT_INST_PROP(inst, absolute_tap_drag_timeout_ms),   \
		.absolute_tap_drag_max_movement =                                                  \
			DT_INST_PROP(inst, absolute_tap_drag_max_movement),                            \
		.absolute_secondary_tap_area_width =                                               \
			DT_INST_PROP(inst, absolute_secondary_tap_area_width),                       \
		.absolute_secondary_tap_area_height =                                              \
			DT_INST_PROP(inst, absolute_secondary_tap_area_height),                      \
		.absolute_aux_tap_area_width = DT_INST_PROP(inst, absolute_aux_tap_area_width),     \
		.absolute_aux_tap_area_height = DT_INST_PROP(inst, absolute_aux_tap_area_height),   \
		.absolute_edge_motion_zone = DT_INST_PROP(inst, absolute_edge_motion_zone),         \
		.absolute_edge_motion_speed = DT_INST_PROP(inst, absolute_edge_motion_speed),       \
		.absolute_edge_motion_interval_ms =                                                \
			DT_INST_PROP(inst, absolute_edge_motion_interval_ms),                          \
		.absolute_edge_motion_start_ms = DT_INST_PROP(inst, absolute_edge_motion_start_ms), \
		.absolute_scroll_zone = DT_INST_PROP(inst, absolute_scroll_zone),                   \
		.absolute_scroll_divisor = DT_INST_PROP(inst, absolute_scroll_divisor),             \
		.sleep_mode_enable = DT_INST_PROP(inst, sleep_mode_enable),                        \
		.clipping_enabled = DT_INST_PROP(inst, clipping_enable),                           \
		.active_range_x_min = DT_INST_PROP(inst, active_range_x_min),                      \
		.active_range_x_max = DT_INST_PROP(inst, active_range_x_max),                      \
		.active_range_y_min = DT_INST_PROP(inst, active_range_y_min),                      \
		.active_range_y_max = DT_INST_PROP(inst, active_range_y_max),                      \
		.scaling_enabled = DT_INST_PROP(inst, scaling_enable),                             \
		.resolution_x = DT_INST_PROP(inst, scaling_x_resolution),                          \
		.resolution_y = DT_INST_PROP(inst, scaling_y_resolution),                          \
		.invert_x = DT_INST_PROP(inst, invert_x),                                          \
		.invert_y = DT_INST_PROP(inst, invert_y),                                          \
		.primary_tap_enabled = DT_INST_PROP(inst, primary_tap_enable),                     \
		.glide_extend_enabled = DT_INST_PROP(inst, glide_extend_enable),                   \
		.absolute_tap_drag_enabled = DT_INST_PROP(inst, absolute_tap_drag_enable),         \
		.absolute_edge_motion_enabled = DT_INST_PROP(inst, absolute_edge_motion_enable),   \
		.absolute_right_edge_scroll_enabled =                                             \
			DT_INST_PROP(inst, absolute_right_edge_scroll_enable),                         \
		.absolute_top_edge_scroll_enabled =                                              \
			DT_INST_PROP(inst, absolute_top_edge_scroll_enable),                           \
		.swap_xy = DT_INST_PROP(inst, swap_xy),                                            \
	};                                                                                         \
	static struct pinnacle_data pinnacle_data_##inst;                                          \
	DEVICE_DT_INST_DEFINE(inst, pinnacle_init, NULL, &pinnacle_data_##inst,                    \
			      &pinnacle_config_##inst, POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY,    \
			      NULL);                                                               \
	BUILD_ASSERT(DT_INST_PROP(inst, active_range_x_min) <                                      \
			     DT_INST_PROP(inst, active_range_x_max),                               \
		     "active-range-x-min must be less than active-range-x-max");                   \
	BUILD_ASSERT(DT_INST_PROP(inst, active_range_y_min) <                                      \
			     DT_INST_PROP(inst, active_range_y_max),                               \
		     "active_range-y-min must be less than active_range-y-max");                   \
	BUILD_ASSERT(DT_INST_PROP(inst, scaling_x_resolution) > 0,                                 \
		     "scaling-x-resolution must be positive");                                     \
	BUILD_ASSERT(DT_INST_PROP(inst, scaling_y_resolution) > 0,                                 \
		     "scaling-y-resolution must be positive");                                     \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, idle_packets_count), 0, UINT8_MAX),               \
		     "idle-packets-count must be in range [0:255]");                               \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, startup_delay_ms), 0, UINT16_MAX),                \
		     "startup-delay-ms must be in range [0:65535]");                              \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_relative_multiplier), 1, UINT16_MAX),    \
		     "absolute-relative-multiplier must be in range [1:65535]");                  \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_relative_divisor), 1, UINT16_MAX),       \
		     "absolute-relative-divisor must be in range [1:65535]");                    \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_touch_min_z), 1, 63),                  \
		     "absolute-touch-min-z must be in range [1:63]");                         \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_tap_max_ms), 1, UINT16_MAX),            \
		     "absolute-tap-max-ms must be in range [1:65535]");                         \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_tap_max_movement), 1, UINT16_MAX),      \
		     "absolute-tap-max-movement must be in range [1:65535]");                   \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_tap_click_ms), 1, UINT16_MAX),         \
		     "absolute-tap-click-ms must be in range [1:65535]");                      \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_tap_drag_timeout_ms), 1,            \
			      UINT16_MAX),                                                        \
		     "absolute-tap-drag-timeout-ms must be in range [1:65535]");            \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_tap_drag_max_movement), 1,          \
			      UINT16_MAX),                                                        \
		     "absolute-tap-drag-max-movement must be in range [1:65535]");          \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_secondary_tap_area_width), 0,           \
			      UINT16_MAX),                                                          \
		     "absolute-secondary-tap-area-width must be in range [0:65535]");          \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_secondary_tap_area_height), 0,          \
			      UINT16_MAX),                                                          \
		     "absolute-secondary-tap-area-height must be in range [0:65535]");         \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_aux_tap_area_width), 0, UINT16_MAX),    \
		     "absolute-aux-tap-area-width must be in range [0:65535]");                \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_aux_tap_area_height), 0, UINT16_MAX),   \
		     "absolute-aux-tap-area-height must be in range [0:65535]");                    \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_edge_motion_zone), 0, UINT16_MAX),       \
		     "absolute-edge-motion-zone must be in range [0:65535]");                      \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_edge_motion_speed), 1, INT16_MAX),       \
		     "absolute-edge-motion-speed must be in range [1:32767]");                    \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_edge_motion_interval_ms), 1,             \
			      UINT16_MAX),                                                        \
		     "absolute-edge-motion-interval-ms must be in range [1:65535]");              \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_edge_motion_start_ms), 0, UINT16_MAX),   \
		     "absolute-edge-motion-start-ms must be in range [0:65535]");                    \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_scroll_zone), 0, UINT16_MAX),          \
		     "absolute-scroll-zone must be in range [0:65535]");                         \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, absolute_scroll_divisor), 1, UINT16_MAX),       \
		     "absolute-scroll-divisor must be in range [1:65535]");

DT_INST_FOREACH_STATUS_OKAY(PINNACLE_DEFINE)
