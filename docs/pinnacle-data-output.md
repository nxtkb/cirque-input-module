# Cirque Pinnacle Data Output

This note summarizes the Cirque Pinnacle data output controls from the
technical documentation and maps them to this driver's Zephyr devicetree
properties.

## Data Feed

Pinnacle starts finger tracking, sampling, and reporting only after the data
feed is enabled. The feed is controlled by `FeedConfig1`, register `0x04`,
bit `0`.

| Register | Bit | Flag | Meaning |
| --- | ---: | --- | --- |
| `0x04` `FeedConfig1` | `0` | Feed Enable | `1` enables the data feed, `0` disables it. |

The driver sets `Feed Enable` during initialization after reset, calibration,
sensitivity setup, and relative-mode feature setup have completed.

## Output Modes

Pinnacle can report positions in two modes:

| Mode | Register setting | Reported data |
| --- | --- | --- |
| Relative | `FeedConfig1[1] = 0` | Motion deltas, like a mouse pointer. Each packet is relative to the previous position. |
| Absolute | `FeedConfig1[1] = 1` | Absolute X/Y/Z touch coordinates on the sensor grid. |

Absolute coordinate ranges from the datasheet:

| ASIC | X range | Y range |
| --- | ---: | ---: |
| Pinnacle | `0` to `2047` | `0` to `1535` |
| Pinnacle AG | `0` to `1919` | `0` to `1407` |

In this module, the `data-mode` devicetree property selects the mode:

```devicetree
glidepoint: glidepoint@2a {
	compatible = "cirque,pinnacle2";
	reg = <0x2a>;
	data-mode = "relative"; /* or "absolute" */
};
```

## `FeedConfig1` Register `0x04`

`FeedConfig1` controls the feed, output mode, filtering, axis reporting, and
axis inversion.

| Bit | Flag | Access | Values | Default | Driver mapping |
| ---: | --- | --- | --- | ---: | --- |
| `7` | Y data invert | R/W | `1`: Y max to `0`; `0`: `0` to Y max | `0` | `invert-y` |
| `6` | X data invert | R/W | `1`: X max to `0`; `0`: `0` to X max | `0` | `invert-x` |
| `4` | Y disable | R/W | `1`: no Y data; `0`: Y data | `0` | Not exposed |
| `3` | X disable | R/W | `1`: no X data; `0`: X data | `0` | Not exposed |
| `2` | Filter disable | R/W | `1`: no filter; `0`: filter enabled | `0` | Not exposed |
| `1` | Data mode | R/W | `1`: absolute; `0`: relative | `0` | `data-mode` |
| `0` | Feed enable | R/W | `1`: feed enabled; `0`: feed disabled | `0` | Always enabled by the driver |

Notes:

- The datasheet describes X/Y inversion as an absolute-mode feature. The
  current driver writes the same hardware bits for both relative and absolute
  mode because they work in relative mode on the supported hardware.
- Disabling either axis prevents normal tracking and is not recommended for
  regular applications.
- The hardware filter is enabled by default. Cirque does not recommend
  disabling it, so this driver does not expose a filter-disable property.

## `FeedConfig2` Register `0x05`

`FeedConfig2` controls advanced features for relative mode.

| Bit | Flag | Access | Values | Default | Driver mapping |
| ---: | --- | --- | --- | ---: | --- |
| `7` | Swap X and Y | R/W | `1`: 90 degree rotation; `0`: no rotation | `0` | `swap-xy` |
| `4` | GlideExtend disable | R/W | `1`: disabled; `0`: enabled | `0` | Driver disables it |
| `3` | Scroll disable | R/W | `1`: disabled; `0`: enabled | `0` | Driver leaves it enabled in relative mode |
| `2` | Secondary tap disable | R/W | `1`: disabled; `0`: enabled | `0` | Not separately exposed |
| `1` | All taps disable | R/W | `1`: disabled; `0`: enabled | `0` | Controlled by `primary-tap-enable` |
| `0` | Intellimouse enable | R/W | `1`: enabled; `0`: disabled | `0` | Driver enables it in relative mode |

Feature notes:

- GlideExtend is Cirque's motion extender feature. It allows a drag operation
  to continue after the finger reaches an edge by lifting and repositioning the
  finger.
- Secondary tap lets a tap in the upper-right corner, in the standard
  orientation, act as the secondary button.
- Disabling all taps also disables secondary taps, even if secondary tap is
  otherwise enabled.
- Intellimouse changes the relative data packet from three bytes to four bytes.
  The fourth byte, `PacketByte_3`, reports scroll wheel count.

## Driver Behavior

The driver programs these registers during `pinnacle_init()`:

| Mode | `FeedConfig2` behavior | `FeedConfig1` behavior |
| --- | --- | --- |
| Relative | Disables GlideExtend, enables Intellimouse, optionally swaps X/Y, and disables all taps unless `primary-tap-enable` is set. | Enables feed, keeps relative mode selected, and applies `invert-x`/`invert-y` if configured. |
| Absolute | Disables GlideExtend, scroll, and all taps. | Enables feed, selects absolute mode, and applies `invert-x`/`invert-y` if configured. |

Relative mode reads four packet bytes so the driver can report wheel count
when Intellimouse mode is enabled.

