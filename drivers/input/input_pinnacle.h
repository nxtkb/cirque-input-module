#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>

/*
 * Register Access Protocol Standard Registers.
 * Standard registers have 5-bit addresses, BIT[4:0], that range from
 * 0x00 to 0x1F. For reading, a register address has to be combined with
 * 0xA0 for reading and 0x80 for writing bits, BIT[7:5].
 */
#define PINNACLE_REG_FIRMWARE_ID      0x00 /* R */
#define PINNACLE_REG_FIRMWARE_VERSION 0x01 /* R */
#define PINNACLE_REG_STATUS1          0x02 /* R/W */
#define PINNACLE_REG_SYS_CONFIG1      0x03 /* R/W */
#define PINNACLE_REG_FEED_CONFIG1     0x04 /* R/W */
#define PINNACLE_REG_FEED_CONFIG2     0x05 /* R/W */
#define PINNACLE_REG_FEED_CONFIG3     0x06 /* R/W */
#define PINNACLE_REG_CAL_CONFIG1      0x07 /* R/W */
#define PINNACLE_REG_PS2_AUX_CONTROL  0x08 /* R/W */
#define PINNACLE_REG_SAMPLE_RATE      0x09 /* R/W */
#define PINNACLE_REG_Z_IDLE           0x0A /* R/W */
#define PINNACLE_REG_Z_SCALER         0x0B /* R/W */
#define PINNACLE_REG_SLEEP_INTERVAL   0x0C /* R/W */
#define PINNACLE_REG_SLEEP_TIMER      0x0D /* R/W */
#define PINNACLE_REG_EMI_THRESHOLD    0x0E /* R/W */
#define PINNACLE_REG_PACKET_BYTE0     0x12 /* R */
#define PINNACLE_REG_PACKET_BYTE1     0x13 /* R */
#define PINNACLE_REG_PACKET_BYTE2     0x14 /* R */
#define PINNACLE_REG_PACKET_BYTE3     0x15 /* R */
#define PINNACLE_REG_PACKET_BYTE4     0x16 /* R */
#define PINNACLE_REG_PACKET_BYTE5     0x17 /* R */
#define PINNACLE_REG_GPIO_A_CTRL      0x18 /* R/W */
#define PINNACLE_REG_GPIO_A_DATA      0x19 /* R/W */
#define PINNACLE_REG_GPIO_B_CTRL_DATA 0x1A /* R/W */
/* Value of the extended register */
#define PINNACLE_REG_ERA_VALUE        0x1B /* R/W */
/* High byte BIT[15:8] of the 16 bit extended register */
#define PINNACLE_REG_ERA_ADDR_HIGH    0x1C /* R/W */
/* Low byte BIT[7:0] of the 16 bit extended register */
#define PINNACLE_REG_ERA_ADDR_LOW     0x1D /* R/W */
#define PINNACLE_REG_ERA_CTRL         0x1E /* R/W */
#define PINNACLE_REG_PRODUCT_ID       0x1F /* R */

/* Extended Register Access */
#define PINNACLE_ERA_REG_CONFIG 0x0187 /* R/W */

/* Firmware ASIC ID value */
#define PINNACLE_FIRMWARE_ID 0x07

/* Status1 definition */
#define PINNACLE_STATUS1_SW_DR BIT(2)
#define PINNACLE_STATUS1_SW_CC BIT(3)

/* SysConfig1 definition */
#define PINNACLE_SYS_CONFIG1_RESET          BIT(0)
#define PINNACLE_SYS_CONFIG1_SHUTDOWN       BIT(1)
#define PINNACLE_SYS_CONFIG1_LOW_POWER_MODE BIT(2)

/* FeedConfig1 definition */
#define PINNACLE_FEED_CONFIG1_FEED_ENABLE        BIT(0)
#define PINNACLE_FEED_CONFIG1_DATA_MODE_ABSOLUTE BIT(1)
#define PINNACLE_FEED_CONFIG1_FILTER_DISABLE     BIT(2)
#define PINNACLE_FEED_CONFIG1_X_DISABLE          BIT(3)
#define PINNACLE_FEED_CONFIG1_Y_DISABLE          BIT(4)
#define PINNACLE_FEED_CONFIG1_X_INVERT           BIT(6)
#define PINNACLE_FEED_CONFIG1_Y_INVERT           BIT(7)
/* X max to 0 */
#define PINNACLE_FEED_CONFIG1_X_DATA_INVERT      BIT(6)
/* Y max to 0 */
#define PINNACLE_FEED_CONFIG1_Y_DATA_INVERT      BIT(7)

/* FeedConfig2 definition */
#define PINNACLE_FEED_CONFIG2_INTELLIMOUSE_ENABLE   BIT(0)
#define PINNACLE_FEED_CONFIG2_ALL_TAPS_DISABLE      BIT(1)
#define PINNACLE_FEED_CONFIG2_SECONDARY_TAP_DISABLE BIT(2)
#define PINNACLE_FEED_CONFIG2_SCROLL_DISABLE        BIT(3)
#define PINNACLE_FEED_CONFIG2_GLIDE_EXTEND_DISABLE  BIT(4)
/* 90 degrees rotation */
#define PINNACLE_FEED_CONFIG2_SWAP_X_AND_Y          BIT(7)

/* Relative position status in PacketByte0 */
#define PINNACLE_PACKET_BYTE0_BTN_PRIMARY  BIT(0)
#define PINNACLE_PACKET_BYTE0_BTN_SECONDRY BIT(1)
#define PINNACLE_PACKET_BYTE0_BTN_AUX BIT(2)

/* Extended Register Access Control */
#define PINNACLE_ERA_CTRL_READ           BIT(0)
#define PINNACLE_ERA_CTRL_WRITE          BIT(1)
#define PINNACLE_ERA_CTRL_READ_AUTO_INC  BIT(2)
#define PINNACLE_ERA_CTRL_WRITE_AUTO_INC BIT(3)
/* Asserting both BIT(1) and BIT(0) means WRITE/Verify */
#define PINNACLE_ERA_CTRL_WRITE_VERIFY   (BIT(1) | BIT(0))
#define PINNACLE_ERA_CTRL_COMPLETE       0x00

/* Extended Register Access Config */
#define PINNACLE_ERA_CONFIG_ADC_ATTENUATION_X1 0x00
#define PINNACLE_ERA_CONFIG_ADC_ATTENUATION_X2 0x40
#define PINNACLE_ERA_CONFIG_ADC_ATTENUATION_X3 0x80
#define PINNACLE_ERA_CONFIG_ADC_ATTENUATION_X4 0xC0

/*
 * Delay and retry count for waiting completion of calibration with 200 ms of
 * timeout.
 */
#define PINNACLE_CALIBRATION_AWAIT_DELAY_POLL_US 50000
#define PINNACLE_CALIBRATION_AWAIT_RETRY_COUNT   4

/*
 * Delay and retry count for waiting completion of ERA command with 50 ms of
 * timeout.
 */
#define PINNACLE_ERA_AWAIT_DELAY_POLL_US 10000
#define PINNACLE_ERA_AWAIT_RETRY_COUNT   5

/* Special definitions */
#define PINNACLE_SPI_FB 0xFB /* Filler byte */
#define PINNACLE_SPI_FC 0xFC /* Auto-increment byte */

/* Read and write masks */
#define PINNACLE_READ_MSK  0xA0
#define PINNACLE_WRITE_MSK 0x80

/* Read and write register addresses */
#define PINNACLE_READ_REG(addr)  (PINNACLE_READ_MSK | addr)
#define PINNACLE_WRITE_REG(addr) (PINNACLE_WRITE_MSK | addr)

struct pinnacle_bus {
	union {
#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
		struct i2c_dt_spec i2c;
#endif
#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)
		struct spi_dt_spec spi;
#endif
	};
	bool (*is_ready)(const struct pinnacle_bus *bus);
	int (*write)(const struct pinnacle_bus *bus, uint8_t address, uint8_t value);
	int (*seq_write)(const struct pinnacle_bus *bus, uint8_t *address, uint8_t *value,
			 uint8_t count);
	int (*read)(const struct pinnacle_bus *bus, uint8_t address, uint8_t *value);
	int (*seq_read)(const struct pinnacle_bus *bus, uint8_t address, uint8_t *data,
			uint8_t count);
};

enum pinnacle_sensitivity {
	PINNACLE_SENSITIVITY_X1,
	PINNACLE_SENSITIVITY_X2,
	PINNACLE_SENSITIVITY_X3,
	PINNACLE_SENSITIVITY_X4,
};

struct pinnacle_config {
	const struct pinnacle_bus bus;
	struct gpio_dt_spec dr_gpio;

	enum pinnacle_sensitivity sensitivity;
	bool relative_mode;
	bool sleep_mode_enable;
	uint8_t idle_packets_count;

	bool clipping_enabled;
	bool scaling_enabled;
	bool invert_x;
	bool invert_y;
	bool primary_tap_enabled;
	bool swap_xy;

	uint16_t active_range_x_min;
	uint16_t active_range_x_max;
	uint16_t active_range_y_min;
	uint16_t active_range_y_max;

	uint16_t resolution_x;
	uint16_t resolution_y;
};

union pinnacle_sample {
	struct {
		uint16_t abs_x;
		uint16_t abs_y;
		uint8_t abs_z;
	};
	struct {
		int16_t rel_x;
		int16_t rel_y;
		bool btn_primary;
		bool btn_secondary;
		bool btn_aux;
		int8_t wheelCount;
	};
};

struct pinnacle_data {
	union pinnacle_sample sample;
	const struct device *dev;
	struct gpio_callback dr_cb_data;
	struct k_work work;
};
