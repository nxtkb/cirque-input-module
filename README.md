# Cirque Pinnacle Input Module for ZMK

This Zephyr module provides a Cirque Pinnacle (`1CA027`) input driver plus ZMK
behaviors/input processors for keyboard-integrated pointing workflows.

It is forked from
[petejohanson/cirque-input-module](https://github.com/petejohanson/cirque-input-module)
and extended for split-keyboard trackpad use.

![Cirque trackpad](images/cirque.png)

## Features

- Cursor movement from the Cirque touchpad.
- Primary/secondary/aux button reporting from firmware taps.
- Right-edge vertical scroll from the Cirque native wheel output.
- Hold-to-drag-scroll behavior: hold a key and move the touchpad to scroll.
- Hold-to-snipe behavior: hold a key and move the touchpad with reduced pointer speed.
- Runtime pointer and scroll speed adjustment.
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
        data-mode = "relative";
        primary-tap-enable;
        sleep-mode-enable;
        invert-y;
    };
};
```

## Behaviors And Processors

Add processors to the ZMK input listener. The recommended order is sniping first,
then drag-scroll:

```dts
&glidepoint_listener {
    input-processors = <&sniping_processor 1 4 &drag_scroll_processor 1 8>;
};
```

Then define the processors and behaviors:

```dts
/ {
    sniping_processor: sniping_processor {
        compatible = "zmk,input-processor-sniping";
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

        snipe: sniping {
            compatible = "zmk,behavior-sniping";
            #binding-cells = <0>;
        };

        ptr_spd: pointing_speed {
            compatible = "zmk,behavior-pointing-speed";
            #binding-cells = <2>;
        };
    };
};
```

The parameters in `&sniping_processor 1 4` are the hold-sniping multiplier and
divisor. In this example, holding `&snipe` applies an extra `1/4` scale to pointer
movement.

The parameters in `&drag_scroll_processor 1 8` are kept for compatibility with
the processor binding. Runtime scroll speed is controlled by the speed table
described below.

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
wheel. The native Cirque right-edge wheel output is also controlled by the same
scroll speed setting.

## Hold-To-Snipe

Bind `&snipe` directly, or use hold-tap for a normal typing key.

Example: tap `C` for `C`, hold `C` for fine pointer movement:

```dts
snipe_c: sniping_c {
    compatible = "zmk,behavior-hold-tap";
    #binding-cells = <2>;
    flavor = "hold-preferred";
    tapping-term-ms = <200>;
    quick-tap-ms = <150>;
    require-prior-idle-ms = <125>;
    bindings = <&snipe>, <&kp>;
};
```

Use it in the keymap:

```dts
&snipe_c 0 C
```

Sniping scales `REL_X/Y` pointer movement. It does not directly scale native
wheel events. If sniping is placed before drag-scroll, holding both the sniping
key and the drag-scroll key produces finer drag-scroll because the pointer delta
is reduced before being converted to wheel movement.

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
sniping_processor: sniping_processor {
    compatible = "zmk,input-processor-sniping";
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
- `C`: tap `C`, hold for sniping/fine pointer movement.
- `MOUSE` layer: pointer speed down/up and scroll speed down/up are placed on
  the left side of the third row.

## Notes

- Native right-edge scrolling is generated by the Cirque firmware as
  `INPUT_REL_WHEEL`.
- Native wheel events are speed-controlled by `drag_scroll_processor`.
- Native wheel events receive a fixed compensation before the scroll speed table
  is applied so the right-edge scroll speed is close to hold-to-scroll speed.
- Button events are reported only on button-state changes, so a keyboard-held
  mouse button is not released by normal touchpad movement.

## Hardware Compatibility

- Controller: Cirque Pinnacle 1CA027 / GlidePoint
- Bus: I2C and SPI
- Tested on: Sweep-Pro with nice!nano v2

## Resources

- ZMK pointing documentation:
  <https://zmk.dev/docs/development/hardware-integration/pointing>
- Cirque Pinnacle data output notes:
  [docs/pinnacle-data-output.md](docs/pinnacle-data-output.md)

## Credits

Thanks to [Pete Johanson](https://github.com/petejohanson/cirque-input-module)
for the original Cirque Pinnacle Zephyr driver.
