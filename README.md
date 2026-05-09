# Cirque Pinnacle Input Module for ZMK

This Zephyr module provides a Cirque Pinnacle (`1CA027`) input driver plus ZMK
behaviors/input processors for keyboard-integrated pointing workflows.

It is forked from
[petejohanson/cirque-input-module](https://github.com/petejohanson/cirque-input-module)
and extended for split-keyboard trackpad use.

![Cirque trackpad](images/cirque.png)

## Features

This module describes features by the user-facing pointing behavior first. The
underlying implementation can be firmware-relative packets, driver-side
absolute-coordinate processing, or ZMK input processors.

- Pointer movement from the Cirque touchpad.
- Primary click from tap.
- Scroll from either native edge wheel, software edge zones, or hold-to-scroll.
- Extended dragging / repositioning behavior, either ASIC-provided or driver-side.
- Runtime pointer and scroll speed adjustment.
- Runtime switching between Cirque relative and absolute data modes.
- User-defined 1x base speed with fine runtime scaling.

## ZMK Build Setup

Add this module to your `west.yml`:

```yaml
manifest:
  remotes:
    - name: nxtkb
      url-base: https://github.com/nxtkb
  projects:
    - name: cirque-input-module
      remote: nxtkb
      revision: main
```

Enable pointing support in your keyboard config:

```conf
CONFIG_I2C=y
CONFIG_I2C_NRFX=y

CONFIG_ZMK_POINTING=y
CONFIG_ZMK_POINTING_SMOOTH_SCROLLING=y
CONFIG_INPUT_THREAD_STACK_SIZE=4096
```

Build with the module included, for example:

```sh
west build -p -d right -b nice_nano//zmk -- \
  -DSHIELD=sweep_right \
  -DZMK_EXTRA_MODULES="/path/to/Sweep-Pro;/path/to/cirque-input-module" \
  -DZMK_CONFIG="/path/to/Sweep-Pro/config"
```

## Device Tree

Use `cirque,pinnacle2` for the trackpad node:

```dts
&i2c0 {
    compatible = "nordic,nrf-twim";
    status = "okay";

    pinctrl-0 = <&i2c0_default>;
    pinctrl-1 = <&i2c0_sleep>;
    pinctrl-names = "default", "sleep";
    clock-frequency = <400000>;

    glidepoint: glidepoint@2a {
        compatible = "cirque,pinnacle2";
        reg = <0x2a>;
        status = "okay";
        data-ready-gpios = <&gpio0 22 GPIO_ACTIVE_HIGH>;
        sensitivity = "2x";
        data-mode = "absolute"; /* or "relative" for the ASIC's stock behavior */
        absolute-relative-divisor = <8>;
        absolute-touch-min-z = <1>;
        primary-tap-enable;
        glide-extend-enable; /* Used only when switching to relative mode. */
        sleep-mode-enable;
        invert-y;
    };
};
```

## Data Modes

`data-mode` chooses how the driver receives raw data from the Pinnacle ASIC. It
should not be read as the final user feature set. The same user-facing feature
can often be implemented in either mode with different tradeoffs.

```dts
data-mode = "relative";
/* or */
data-mode = "absolute";
```

If omitted, `data-mode` defaults to `absolute`. Set it to `relative` explicitly
when you want the ASIC's stock relative packet and GlideExtend behavior at boot.

### User-Facing Feature Map

| Feature | Relative mode implementation | Absolute mode implementation | Current absolute support |
| --- | --- | --- | --- |
| Pointer movement | Firmware emits `REL_X/Y` packets. | Driver reads absolute coordinates and emits coordinate deltas as `REL_X/Y`. | Supported. |
| Primary tap / click | Firmware tap button bits are forwarded as `INPUT_BTN_0`. | Driver detects short, low-movement touches and emits `INPUT_BTN_0`; optional tap-drag holds the primary button on the second touch. | Supported. |
| Secondary / auxiliary tap | Firmware tap button bits can expose secondary and auxiliary buttons. | Driver classifies configurable absolute tap zones, lower-right as secondary and optional upper-left as auxiliary. | Supported when the zone width/height properties are non-zero. |
| Edge scroll | Firmware emits native wheel packets from the right edge. | Driver can lock a touch-start edge zone and convert movement to wheel events: right edge for vertical scroll, top edge for horizontal scroll. | Supported with `absolute-right-edge-scroll-enable` / `absolute-top-edge-scroll-enable`. |
| Hold-to-scroll | ZMK processor converts pointer deltas to wheel events while drag-scroll is enabled. | Same, because absolute mode still emits `REL_X/Y`. | Supported. |
| Pointer speed | ZMK processor scales `REL_X/Y`. | Same, because absolute mode still emits `REL_X/Y`. | Supported. |
| Edge auto-pan / continued motion | ASIC GlideExtend can continue motion after lift-and-reposition gestures. | Driver can emit continued pointer motion while a finger is held in an edge zone. | Supported with `absolute-edge-motion-enable`. |
| Axis transform | `invert-x`, `invert-y`, and `swap-xy` are applied by Pinnacle feed configuration. | Driver applies transforms while converting absolute coordinates to deltas. | `invert-x` and `swap-xy` supported; `invert-y` is intentionally not applied in the current tested orientation. |

### Relative Mode

`data-mode = "relative"` asks the Pinnacle firmware to produce mouse-like
relative packets. This is closest to the ASIC's stock behavior. The driver
mostly forwards firmware-generated motion, wheel, and button state into ZMK
input events.

Relative mode is useful when:

- You want native right-edge wheel behavior.
- You want firmware-provided tap buttons, including secondary / auxiliary tap.
- You want to compare against the ASIC's stock relative behavior.
- You want to use the ASIC GlideExtend implementation.

Relative mode tradeoffs:

- Gesture thresholds such as GlideExtend edge detection are controlled inside
  the ASIC and are not very tunable from the driver.
- The driver receives deltas, not full contact position, so software gestures
  based on absolute location are limited.

### Absolute Mode

`data-mode = "absolute"` asks the Pinnacle firmware for absolute coordinates.
The driver then builds mouse-like behavior in software and still emits `REL_X/Y`
so existing ZMK processors keep working.

Absolute mode is useful when:

- You want driver-side control over gesture thresholds and edge zones.
- You want to experiment with software GlideExtend or other touchpad gestures.
- You want features to be expressed as user-facing behavior instead of relying
  on the ASIC's relative-mode state machine.

Current absolute-mode behavior:

- The first touch establishes a baseline. When software taps are enabled, small
  startup movement is suppressed until the touch exceeds
  `absolute-tap-max-movement`; after that, packets become `REL_X/Y` pointer
  deltas until the finger lifts. Without software taps, absolute movement starts
  immediately after the first baseline packet.
- A forced idle packet after lift resets the baseline and avoids jumps between
  strokes.
- `primary-tap-enable` enables software tap detection. By default it emits
  `INPUT_BTN_0`; configured lower-right and optional upper-left zones can emit
  secondary and auxiliary button events.
- `absolute-tap-max-ms` and `absolute-tap-max-movement` tune software tap
  recognition. `absolute-tap-click-ms` controls how long the generated mouse
  button is held before release. `absolute-tap-drag-enable` enables
  double-tap-drag text selection / dragging; the second touch only presses the
  primary button after it moves beyond `absolute-tap-max-movement`.
  `absolute-tap-drag-timeout-ms` and `absolute-tap-drag-max-movement` control
  whether that second touch is considered part of the prior tap.
- `absolute-touch-min-z` sets the minimum absolute-mode pressure treated as a
  real touch. Increase it if very light contact or hover-like samples cause
  unwanted pointer movement.
- `absolute-secondary-tap-area-width`, `absolute-secondary-tap-area-height`,
  `absolute-aux-tap-area-width`, and `absolute-aux-tap-area-height` define
  optional corner tap zones. A width or height of 0 disables that zone.
- `absolute-relative-multiplier` and `absolute-relative-divisor` tune cursor
  scale. Increase the divisor for slower movement; decrease it for faster
  movement.
- `absolute-edge-motion-enable` enables touchpad-style edge auto-pan: holding a
  finger in an edge zone keeps emitting pointer motion until the finger leaves
  the zone or lifts. `absolute-edge-motion-zone`, `absolute-edge-motion-speed`,
  `absolute-edge-motion-interval-ms`, and `absolute-edge-motion-start-ms` tune
  the behavior.
- `absolute-right-edge-scroll-enable` locks a touch that starts in the logical
  right edge zone into vertical scrolling. `absolute-top-edge-scroll-enable`
  does the same for horizontal scrolling from the logical top edge.
  `absolute-scroll-zone` controls the zone width, and
  `absolute-scroll-divisor` controls scroll speed. Tap-drag takes priority over
  edge scrolling so double-tap-drag still selects text / drags objects.
- `invert-scroll` on the drag-scroll input processor reverses wheel direction
  for edge scrolling, hold-to-scroll, and native wheel packets. Mouse-key scroll
  bindings are regular ZMK keymap entries and should be reversed in the keymap
  if desired.

Absolute mode tradeoffs:

- Firmware-only features such as native wheel packets and firmware secondary /
  auxiliary tap bits are not available directly. They need software equivalents.
- Software equivalents can be more tunable, but they must be implemented and
  calibrated in the driver.

## Behaviors And Processors

Add processors to the ZMK input listener. The recommended order is pointer-speed
first, then drag-scroll:

```dts
&glidepoint_listener {
    input-processors = <&pointer_processor 0 0 &drag_scroll_processor 1 8>;
};
```

Then define the processors and behaviors:

```dts
/ {
    pointer_processor: pointer_processor {
        compatible = "zmk,input-processor-pointer-speed";
        #input-processor-cells = <2>;
        track-remainders;
        one-x-multiplier = <1>;
        one-x-divisor = <1>;
        min-percent = <10>;
        max-percent = <400>;
        initial-speed-position = <50>;
    };

    drag_scroll_processor: drag_scroll_processor {
        compatible = "zmk,input-processor-drag-scroll";
        #input-processor-cells = <2>;
        track-remainders;
        one-x-multiplier = <1>;
        one-x-divisor = <1>;
        min-percent = <10>;
        max-percent = <1000>;
        initial-speed-position = <50>;
    };

    behaviors {
        drgscrl: drag_scroll {
            compatible = "zmk,behavior-drag-scroll";
            #binding-cells = <0>;
        };

        ptr_spd: pointing_speed {
            compatible = "zmk,behavior-pointing-speed";
            #binding-cells = <2>;
        };

        crq_mode: cirque_mode {
            compatible = "zmk,behavior-cirque-mode";
            #binding-cells = <1>;
        };
    };
};
```

The parameters in `&pointer_processor 0 0` are kept for compatibility with the
processor binding. Runtime pointer speed is controlled by the fine speed
position described below.

The parameters in `&drag_scroll_processor 1 8` are kept for compatibility with
the processor binding. Runtime scroll speed is controlled by the fine speed
position described below.

This module no longer provides a separate hold-to-snipe behavior. Fine pointer
movement is handled by the pointer-speed processor and the `&ptr_spd` runtime
speed bindings.

## Hold-To-Scroll

Bind `&drgscrl` directly, or wrap it in hold-tap so a normal key can become
hold-to-scroll.

Example: tap `Z` for `Z`, hold `Z` and move the touchpad to scroll:

```dts
ds_z: drag_scroll_z {
    compatible = "zmk,behavior-hold-tap";
    #binding-cells = <2>;
    flavor = "hold-preferred";
    tapping-term-ms = <200>;
    quick-tap-ms = <150>;
    require-prior-idle-ms = <125>;
    bindings = <&drgscrl>, <&kp>;
};
```

Use it in the keymap:

```dts
&ds_z 0 Z
```

When held, touchpad `REL_X` becomes horizontal wheel and `REL_Y` becomes vertical
wheel. In relative mode, the native Cirque right-edge wheel output is also
controlled by the same scroll speed setting.

## Runtime Speed Control

`&ptr_spd` changes the current fine speed position at runtime:

```dts
#define SPEED_POINTER 0
#define SPEED_SCROLL 1
#define SPEED_PREV 0
#define SPEED_NEXT 1
#define SPEED_RESET 2
#define SPEED_FINE_PREV 3
#define SPEED_FINE_NEXT 4
```

Bindings:

```dts
&ptr_spd SPEED_POINTER SPEED_PREV
&ptr_spd SPEED_POINTER SPEED_NEXT
&ptr_spd SPEED_POINTER SPEED_RESET
&ptr_spd SPEED_POINTER SPEED_FINE_PREV
&ptr_spd SPEED_POINTER SPEED_FINE_NEXT
&ptr_spd SPEED_SCROLL SPEED_PREV
&ptr_spd SPEED_SCROLL SPEED_NEXT
&ptr_spd SPEED_SCROLL SPEED_RESET
&ptr_spd SPEED_SCROLL SPEED_FINE_PREV
&ptr_spd SPEED_SCROLL SPEED_FINE_NEXT
```

The runtime position range is `0..100`. Position `50` is always `1x`.
Positions below `50` scale linearly from `0.1x` to `1x`; positions above `50`
use the built-in exponential curves for the common `4x` pointer and `10x`
scroll ranges. Other `max-percent` values use a linear fallback. Coarse
`SPEED_PREV` / `SPEED_NEXT` actions move the runtime position by one. Fine
`SPEED_FINE_PREV` / `SPEED_FINE_NEXT` actions adjust the current multiplier by
`0.01x`. Runtime adjustments clamp at the ends instead of wrapping.
`SPEED_RESET` returns the selected target to position `50`, i.e. `1x`.

When settings are enabled, pointer and scroll speeds are saved per selected ZMK
endpoint: USB has one state and each BLE profile has its own state. If no saved
setting exists, each processor starts from its `initial-speed-position`.

## Runtime Data Mode Control

`&crq_mode` switches the Cirque data mode at runtime. `data-mode` in devicetree
still controls the boot default.

```dts
#define CIRQUE_MODE_RELATIVE 0
#define CIRQUE_MODE_ABSOLUTE 1
#define CIRQUE_MODE_TOGGLE 2
```

Bindings:

```dts
&crq_mode CIRQUE_MODE_RELATIVE
&crq_mode CIRQUE_MODE_ABSOLUTE
&crq_mode CIRQUE_MODE_TOGGLE
```

Switching mode reprograms the Pinnacle feed configuration and clears current
touch state. For the cleanest feel, trigger the behavior while no finger is on
the touchpad. The behavior applies to every ready `cirque,pinnacle2` device in
the firmware image.

When settings are enabled, the selected relative / absolute mode is saved after
runtime changes and restored after reboot. `data-mode` in devicetree remains the
fallback used when no saved setting exists.

## Base Speed And Runtime Scaling

Both processors support a user-defined 1x base speed:

- `one-x-multiplier`
- `one-x-divisor`
- `min-percent`
- `max-percent`
- `initial-speed-position`

The base speed is the scale applied at runtime position `50`. Runtime position
then applies a multiplier on top of that base speed.

For the exact pointer, hold-to-scroll, native wheel, and absolute-mode edge
scroll formulas, see
[Pointer And Scroll Speed Calculation](docs/speed-calculation.md).

Scroll also applies a small acceleration curve after runtime scaling. The
default curve leaves small deltas unchanged and boosts larger deltas:

```dts
scroll-curve-deadzone = <2>;
scroll-curve-accel-multiplier = <1>;
scroll-curve-accel-divisor = <16>;
```

Pointer speed defaults to `1x` at position `50`, with a `0.1x..4x` runtime
range:

```dts
one-x-multiplier = <1>;
one-x-divisor = <1>;
min-percent = <10>;
max-percent = <400>;
initial-speed-position = <50>;
```

Scroll speed defaults to `1x` at position `50`, with a `0.1x..10x` runtime
range:

```dts
one-x-multiplier = <1>;
one-x-divisor = <1>;
min-percent = <10>;
max-percent = <1000>;
initial-speed-position = <50>;
```

Example with a slower custom pointer 1x speed:

```dts
pointer_processor: pointer_processor {
    compatible = "zmk,input-processor-pointer-speed";
    #input-processor-cells = <2>;
    track-remainders;
    one-x-multiplier = <3>;
    one-x-divisor = <4>;
    min-percent = <10>;
    max-percent = <400>;
    initial-speed-position = <50>;
};
```

## Sweep-Pro Example Layout

The current Sweep-Pro setup uses these conventions:

- `Z`: tap `Z`, hold for drag-scroll.
- `X`: tap `X`, hold for left click/selection.
- `C`: normal typing key.
- `MOUSE` layer: left encoder adjusts pointer speed; right encoder adjusts
  scroll speed. The left side of the third row can also provide fine pointer
  down/up and fine scroll down/up bindings.

## Notes

- Relative-mode native right-edge scrolling is generated by the Cirque firmware
  as `INPUT_REL_WHEEL`.
- Relative-mode native wheel events are speed-controlled by `drag_scroll_processor`.
- Relative-mode native wheel events receive a fixed compensation before scroll
  scaling is applied so the right-edge scroll speed is close to hold-to-scroll speed.
- Button events are reported only on button-state changes, so a keyboard-held
  mouse button is not released by normal touchpad movement.

## Hardware Compatibility

- Controller: Cirque Pinnacle 1CA027 / GlidePoint
- Bus: I2C and SPI
- Tested on: Sweep-Pro with nice!nano v2

## Resources

- ZMK pointing documentation:
  <https://zmk.dev/docs/development/hardware-integration/pointing>
- Cirque Pinnacle interface and register-access notes:
  [docs/pinnacle-interface.md](docs/pinnacle-interface.md)
- Cirque Pinnacle data output notes:
  [docs/pinnacle-data-output.md](docs/pinnacle-data-output.md)
- Pointer and scroll speed calculation:
  [docs/speed-calculation.md](docs/speed-calculation.md)

## Credits

Thanks to [Pete Johanson](https://github.com/petejohanson/cirque-input-module)
for the original Cirque Pinnacle Zephyr driver.
