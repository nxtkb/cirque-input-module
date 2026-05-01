# Cirque Pinnacle Interface Notes

This note summarizes the hardware-interface parts of Cirque's
`Interfacing to the Pinnacle ASIC Using SPI and I2C` application note and maps
those details to this module's driver. It intentionally leaves the packet-format
and feed-feature details in [pinnacle-data-output.md](pinnacle-data-output.md).

## Scope

The application note targets Pinnacle 2.2 and Pinnacle AG 1.4. This driver is
built around the standard Pinnacle register layout used by the Cirque `1CA027`
controller. The driver exposes the same bus abstraction for I2C and SPI through
`struct pinnacle_bus` in `drivers/input/input_pinnacle.h`.

## Standard Register Access

Pinnacle uses a small Register Access Protocol (RAP) for the standard register
set. Standard registers have 5-bit addresses from `0x00` through `0x1F`.

| Operation | Command mask | Driver helper |
| --- | ---: | --- |
| Read | `0xA0 | address` | `PINNACLE_READ_REG(addr)` / `pinnacle_read()` |
| Write | `0x80 | address` | `PINNACLE_WRITE_REG(addr)` / `pinnacle_write()` |

The important maintenance rule is read-modify-write for shared configuration
registers: read the register, change only the intended bits, then write it back.
This driver usually builds full register values during init because it owns the
startup configuration, but feature additions should still avoid clearing
unrelated bits accidentally.

## Standard Registers Used By The Driver

| Register | Name | Driver constant | Current use |
| ---: | --- | --- | --- |
| `0x00` | Firmware ID | `PINNACLE_REG_FIRMWARE_ID` | Validate the expected ASIC after reset. |
| `0x02` | Status1 | `PINNACLE_REG_STATUS1` | Clear `SW_CC` / `SW_DR`; poll command completion after reset. |
| `0x03` | SysConfig1 | `PINNACLE_REG_SYS_CONFIG1` | Software reset, shutdown, low-power sleep. |
| `0x04` | FeedConfig1 | `PINNACLE_REG_FEED_CONFIG1` | Enable feed and select relative/absolute data mode. |
| `0x05` | FeedConfig2 | `PINNACLE_REG_FEED_CONFIG2` | Relative-mode features such as Intellimouse, taps, GlideExtend, swap X/Y. |
| `0x09` | Sample Rate | `PINNACLE_REG_SAMPLE_RATE` | Defined for future use; not currently configured. |
| `0x0A` | ZIdle | `PINNACLE_REG_Z_IDLE` | Configure no-touch idle packets after lift. |
| `0x12..0x17` | Packet bytes | `PINNACLE_REG_PACKET_BYTE*` | Read relative packets or absolute coordinate bytes. |
| `0x1B..0x1E` | ERA window | `PINNACLE_REG_ERA_*` | Access extended registers for sensitivity. |

## Data Ready And Status1

The hardware data-ready pin (`HW_DR`) is active high. It is asserted when either
of these `Status1` flags is set:

| Flag | Register bit | Meaning | Driver behavior |
| --- | ---: | --- | --- |
| `SW_DR` | `Status1[2]` | New touch data is available. | The GPIO callback schedules driver work; after the packet read the driver writes `0x00` to `Status1`. |
| `SW_CC` | `Status1[3]` | Command complete after power-on reset, calibration, or ERA command. | Reset and ERA helpers poll for completion and clear `Status1`. |

While touch is active the ASIC can update position data about every 10 ms. The
host must clear `Status1` after handling data so `HW_DR` can deassert and later
assert again for new data. The driver centralizes this through
`pinnacle_clear_cmd_complete()` and the final `Status1` write in
`pinnacle_sample_fetch()`.

## Startup Sequence

The application note's typical sequence is:

1. Wait for power-on reset / calibration command completion.
2. Clear `Status1` by writing `0x00`.
3. Configure `SysConfig1` and `FeedConfig2`.
4. Configure `FeedConfig1` to select the output mode and enable the feed.
5. On each `HW_DR`, read the packet registers, clear `Status1`, then process the packet.

This driver follows the same shape with a few practical additions:

| Step | Driver implementation |
| --- | --- |
| Startup delay | Optional `startup-delay-ms` handles MCU resets without power-cycling the ASIC. |
| Reset | `pinnacle_soft_reset()` writes the reset bit and waits for `SW_CC`. |
| ASIC check | `pinnacle_read_firmware_id()` validates `PINNACLE_FIRMWARE_ID`. |
| Sensitivity | `pinnacle_set_sensitivity()` writes the extended config register through ERA. |
| Mode setup | `pinnacle_init()` writes `SysConfig1`, `FeedConfig2`, `FeedConfig1`, and `ZIdle`. |
| Interrupt setup | `pinnacle_init_interrupt()` connects `HW_DR` to the Zephyr input work path. |

## I2C Notes

Pinnacle's default 7-bit I2C address is `0x2A`, matching the common devicetree
node form:

```dts
glidepoint: glidepoint@2a {
    compatible = "cirque,pinnacle2";
    reg = <0x2a>;
};
```

For I2C reads, the host first writes a RAP read command to set Pinnacle's
current read address. A following I2C read returns bytes starting at that address
and auto-increments until the transfer stops. The driver maps this to
`pinnacle_seq_read_i2c()` with `i2c_write_read_dt()`.

For I2C writes, the RAP write command byte and the value byte belong to the same
transaction. The driver maps single and repeated writes through
`pinnacle_write_i2c()` and `pinnacle_seq_write_i2c()`.

## SPI Notes

Pinnacle is an SPI slave. The application note specifies CPOL = 0 and CPHA = 1,
MSB first, with slave-select low while the device is active. The driver encodes
that as:

```c
#define PINNACLE_SPI_OP (SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_MODE_CPHA | SPI_WORD_SET(8))
```

SPI reads are pipelined. After the RAP read command, filler bytes are required
before the requested register value appears. The driver constants are:

| Byte | Driver constant | Meaning |
| ---: | --- | --- |
| `0xFB` | `PINNACLE_SPI_FB` | Plain filler byte. |
| `0xFC` | `PINNACLE_SPI_FC` | Auto-increment filler byte for sequential register reads. |

`pinnacle_seq_read_spi()` uses the auto-increment path for packet reads.
`pinnacle_write_spi()` sends the RAP write command followed by the value.

## Extended Register Access

Extended Register Access (ERA) exposes a larger register space through four
standard RAP registers:

| RAP register | Driver constant | Purpose |
| ---: | --- | --- |
| `0x1B` | `PINNACLE_REG_ERA_VALUE` | Value to read or write. |
| `0x1C` | `PINNACLE_REG_ERA_ADDR_HIGH` | High byte of the 16-bit extended address. |
| `0x1D` | `PINNACLE_REG_ERA_ADDR_LOW` | Low byte of the 16-bit extended address. |
| `0x1E` | `PINNACLE_REG_ERA_CTRL` | ERA command and auto-increment control. |

`PINNACLE_ERA_CTRL_READ`, `PINNACLE_ERA_CTRL_WRITE`,
`PINNACLE_ERA_CTRL_READ_AUTO_INC`, and `PINNACLE_ERA_CTRL_WRITE_AUTO_INC` map to
the ERA control bits. When an ERA command completes the control register returns
to `0x00`. ERA access asserts `SW_CC` and therefore `HW_DR`, so the driver waits
for completion and clears `Status1` afterward.

The application note recommends disabling the data feed while accessing extended
registers. The current driver performs ERA sensitivity setup before enabling the
feed during initialization, which satisfies that requirement.

## Packet Register Addressing

The application note documents different packet-byte addresses for Pinnacle 2.2
and Pinnacle AG. This driver currently uses the Pinnacle packet-byte aliases
`0x12..0x17` and reads:

| Driver mode | Read start | Length | Meaning |
| --- | ---: | ---: | --- |
| Relative | `PINNACLE_REG_PACKET_BYTE0` (`0x12`) | 4 | Button/sign byte, X delta, Y delta, wheel count when Intellimouse is enabled. |
| Absolute | `PINNACLE_REG_PACKET_BYTE2` (`0x14`) | 4 | X low, Y low, high X/Y bits, Z level. |

That absolute read intentionally skips the button/switch status byte because the
current absolute-mode implementation builds pointer, tap, right-click zone, edge
scroll, and tap-drag behavior in software.

## Implementation Checklist For Driver Changes

When adding or changing Pinnacle features, check these points:

- Does the feature require relative firmware output, absolute coordinates, or an
  extended register?
- If it writes a shared register, does it preserve unrelated bits?
- If it uses ERA, is the feed disabled or not yet enabled, and is `SW_CC` cleared
  afterward?
- If it changes packet length, does `pinnacle_sample_fetch()` read the matching
  number of bytes and clear `Status1` once per packet?
- If it changes interrupt behavior, does `HW_DR` still deassert after both
  `SW_DR` and `SW_CC` are cleared?
