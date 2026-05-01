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
- User-defined speed tables with any number of speed levels.

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
        data-mode = "relative"; /* or "absolute" for driver-side delta tracking */
        absolute-relative-divisor = <8>;
        absolute-touch-min-z = <1>;
        primary-tap-enable;
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

- The first touch establishes a baseline. Later packets become `REL_X/Y`
  pointer deltas until the finger lifts.
- A forced idle packet after lift resets the baseline and avoids jumps between
  strokes.
- `primary-tap-enable` enables software tap detection. By default it emits
  `INPUT_BTN_0`; configured lower-right and optional upper-left zones can emit
  secondary and auxiliary button events.
- `absolute-tap-max-ms` and `absolute-tap-max-movement` tune software tap
  recognition. `absolute-tap-click-ms` controls how long the generated mouse
  button is held before release. `absolute-tap-drag-enable` enables
  double-tap-drag text selection / dragging, with
  `absolute-tap-drag-timeout-ms` and `absolute-tap-drag-max-movement` for
  calibration.
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
    };

    drag_scroll_processor: drag_scroll_processor {
        compatible = "zmk,input-processor-drag-scroll";
        #input-processor-cells = <2>;
        track-remainders;
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
processor binding. Runtime pointer speed is controlled by the speed table
described below.

The parameters in `&drag_scroll_processor 1 8` are kept for compatibility with
the processor binding. Runtime scroll speed is controlled by the speed table
described below.

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

`&ptr_spd` changes the current speed level at runtime:

```dts
#define SPEED_POINTER 0
#define SPEED_SCROLL 1
#define SPEED_PREV 0
#define SPEED_NEXT 1
```

Bindings:

```dts
&ptr_spd SPEED_POINTER SPEED_PREV
&ptr_spd SPEED_POINTER SPEED_NEXT
&ptr_spd SPEED_SCROLL SPEED_PREV
&ptr_spd SPEED_SCROLL SPEED_NEXT
```

The current speed is runtime state only. After reboot, each processor returns to
its `initial-speed-index`.

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

## Custom Speed Tables

Both processors support speed tables:

- `speed-multipliers`
- `speed-divisors`
- `initial-speed-index`

The multiplier and divisor arrays must have the same length. That length is the
number of speed levels. Any number of levels is allowed.

Pointer speed defaults to 3 levels:

```dts
speed-multipliers = <1 1 3>;
speed-divisors = <2 1 2>;
initial-speed-index = <1>;
```

That gives `1/2`, `1/1`, and `3/2`, starting at the middle level.

Scroll speed defaults to 3 levels:

```dts
speed-multipliers = <1 1 1>;
speed-divisors = <12 8 4>;
initial-speed-index = <1>;
```

That gives `1/12`, `1/8`, and `1/4`, starting at the middle level.

Example with 5 custom pointer levels:

```dts
pointer_processor: pointer_processor {
    compatible = "zmk,input-processor-pointer-speed";
    #input-processor-cells = <2>;
    track-remainders;
    speed-multipliers = <1 2 1 3 2>;
    speed-divisors = <4 3 1 2 1>;
    initial-speed-index = <2>;
};
```

## Sweep-Pro Example Layout

The current Sweep-Pro setup uses these conventions:

- `Z`: tap `Z`, hold for drag-scroll.
- `X`: tap `X`, hold for left click/selection.
- `C`: normal typing key.
- `MOUSE` layer: pointer speed down/up and scroll speed down/up are placed on
  the left side of the third row.

## Notes

- Relative-mode native right-edge scrolling is generated by the Cirque firmware
  as `INPUT_REL_WHEEL`.
- Relative-mode native wheel events are speed-controlled by `drag_scroll_processor`.
- Relative-mode native wheel events receive a fixed compensation before the scroll
  speed table is applied so the right-edge scroll speed is close to hold-to-scroll speed.
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

## Credits

Thanks to [Pete Johanson](https://github.com/petejohanson/cirque-input-module)
for the original Cirque Pinnacle Zephyr driver.
