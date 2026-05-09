# Pointer And Scroll Speed Calculation

This note describes how pointer and scroll speed are calculated by the Cirque
driver and ZMK input processors in this module.

## Processing Stages

The usual input processor order is:

```dts
input-processors = <&pointer_processor 0 0 &drag_scroll_processor 1 8>;
```

With that order:

1. The Cirque driver emits input events.
2. `pointer_processor` handles `INPUT_REL_X` and `INPUT_REL_Y` only when
   drag-scroll is not active.
3. `drag_scroll_processor` handles native wheel events and converts
   `INPUT_REL_X/Y` to wheel events only when drag-scroll is active.

This means hold-to-scroll does not use pointer-scaled motion as its input. When
drag-scroll is active, `pointer_processor` intentionally leaves `INPUT_REL_X/Y`
unchanged, then `drag_scroll_processor` applies scroll scaling to those
raw deltas.

## Runtime Speed Positions

Pointer speed and scroll speed use separate runtime positions. Each position is
an integer from `0` to `100`:

- `0`: `min-percent`, normally `0.1x`
- `50`: `1x`
- `100`: `max-percent`, normally `4x` for pointer and `10x` for scroll

The processor configuration defines the 1x base speed:

```dts
one-x-multiplier = <1>;
one-x-divisor = <1>;
min-percent = <10>;
max-percent = <400>;
initial-speed-position = <50>;
```

For each input event:

```text
runtime_multiplier = multiplier_for_position(runtime_position)
numerator = input_delta * one-x-multiplier * runtime_multiplier
denominator = one-x-divisor
scaled = (numerator + remainder) / denominator
remainder = numerator + old_remainder - scaled * denominator
```

The runtime multiplier is stored as fixed-point Q16.16 internally, so the real
denominator is `one-x-divisor * 65536`. Integer division truncates toward zero.
If a divisor is configured as `0`, the processor treats it as `1`.

When `track-remainders` is enabled, fractional movement is carried into later
events through `remainder`. Without it, any fractional part from integer
division is discarded.

The lower half of the position range is linear:

```text
0..50: 0.1x -> 1.0x
```

The upper half uses built-in exponential curves for the common `4x` pointer and
`10x` scroll ranges. Other `max-percent` values use a linear fallback:

```text
50..100: 1.0x -> max-percent
```

`initial-speed-position = <50>` is the fallback boot position. When settings are
enabled, runtime pointer and scroll speeds are saved per selected ZMK endpoint:
USB has one state and each BLE profile has its own state. Saved and configured
positions are clamped to `0..100`; runtime adjustments clamp at the ends.

## Pointer Processor

`pointer_processor` handles `INPUT_REL_X` and `INPUT_REL_Y` when drag-scroll is
not active:

```text
pointer_delta = speed_position_scale(input_delta)
pointer_delta = pointer_curve(pointer_delta)
pointer_delta = clamp_to_int16(pointer_delta)
```

The default `pointer-curve` is `"linear"`, so the curve stage returns the value
unchanged.

When `pointer-curve = "adaptive"`:

```text
magnitude = abs(pointer_delta)

if magnitude <= pointer-curve-deadzone:
    curved = magnitude
else:
    over = magnitude - pointer-curve-deadzone
    curved = magnitude + over * over
             * pointer-curve-accel-multiplier
             / pointer-curve-accel-divisor

if pointer-curve-max-delta > 0:
    curved = min(curved, pointer-curve-max-delta)

pointer_delta = sign(pointer_delta) * curved
```

So runtime scaling is the base scale, and the adaptive curve optionally adds
extra acceleration for larger deltas.

## Drag-Scroll Processor

`drag_scroll_processor` uses the scroll speed position and scroll scaling. It
handles three kinds of input:

| Input event | When handled | Output event | Extra scaling |
| --- | --- | --- | --- |
| `INPUT_REL_X` | Drag-scroll active | `INPUT_REL_HWHEEL` | Scroll scaling, then scroll curve |
| `INPUT_REL_Y` | Drag-scroll active | `INPUT_REL_WHEEL` | Scroll scaling, scroll curve, then sign flip |
| `INPUT_REL_WHEEL/HWHEEL` | Always | Wheel / horizontal wheel | `NATIVE_WHEEL_SPEED_BOOST`, scroll scaling, then scroll curve |

For hold-to-scroll, the formula is:

```text
wheel_delta = scroll_curve(speed_position_scale(raw_pointer_delta))
```

For Y movement, the processor flips the sign after scaling so moving the finger
in the expected direction produces the expected vertical wheel direction.

For native wheel events, such as relative-mode right-edge scrolling from the
Pinnacle ASIC, the processor first multiplies the configured 1x base multiplier
by `NATIVE_WHEEL_SPEED_BOOST`, currently `8`:

```text
native_wheel_delta = scroll_curve(
    speed_position_scale(raw_wheel_delta, one-x-multiplier * 8, one-x-divisor)
)
```

This compensation exists because native wheel packets are much smaller than
pointer motion deltas.

The scroll curve is always part of the drag-scroll processor. Runtime scaling
controls the base scale; the curve only adds extra response for larger deltas:

```text
magnitude = abs(scaled)

if magnitude <= scroll-curve-deadzone:
    curved = magnitude
else:
    over = magnitude - scroll-curve-deadzone
    curved = magnitude + over * over
             * scroll-curve-accel-multiplier
             / scroll-curve-accel-divisor

scaled = sign(scaled) * curved
```

The default values are:

```dts
scroll-curve-deadzone = <2>;
scroll-curve-accel-multiplier = <1>;
scroll-curve-accel-divisor = <16>;
```

Set `scroll-curve-accel-multiplier = <0>` if the curve should have no effect.

After the scroll table and scroll curve, `scroll-min-step` and
`scroll-max-delta` are applied:

```text
if scroll-min-step > 0 and abs(scaled) < scroll-min-step:
    emit 0 and keep the accumulated value as remainder

if scroll-max-delta > 0 and abs(scaled) > scroll-max-delta:
    emit sign(scaled) * scroll-max-delta and clear remainder
```

With the default `scroll-max-delta = <0>`, there is no per-event cap.

Finally, `invert-scroll` reverses the wheel value if enabled. Optional scroll
inertia is scheduled after these calculations.

## Absolute Mode Driver Scaling

When the Cirque device uses `data-mode = "absolute"`, the driver first converts
absolute coordinate movement into relative events.

Pointer movement:

```text
driver_rel_delta = coordinate_delta
                 * absolute-relative-multiplier
                 / absolute-relative-divisor
```

The resulting `INPUT_REL_X/Y` events then pass through `pointer_processor`, so
absolute-mode pointer speed is affected by both:

- `absolute-relative-multiplier` / `absolute-relative-divisor`
- the pointer processor runtime speed position and optional pointer curve

Absolute edge scrolling:

```text
scroll_remainder += coordinate_delta
wheel_ticks = scroll_remainder / absolute-scroll-divisor
scroll_remainder %= absolute-scroll-divisor
```

Those emitted `INPUT_REL_WHEEL/HWHEEL` events then pass through
`drag_scroll_processor`, so absolute edge scrolling is affected by both:

- `absolute-scroll-divisor`
- the scroll processor runtime speed position, native wheel boost, min/max step handling,
  scroll curve, and optional `invert-scroll`

## Practical Tuning Notes

- To change the baseline pointer feel, tune `one-x-multiplier` and
  `one-x-divisor` on `pointer_processor`.
- To change the baseline hold-to-scroll feel, tune `one-x-multiplier` and
  `one-x-divisor` on `drag_scroll_processor`.
- To adjust speed at runtime, use `&ptr_spd SPEED_POINTER ...` or
  `&ptr_spd SPEED_SCROLL ...` bindings, commonly from encoders.
- Use the fine actions to adjust the active multiplier by `0.01x` without
  moving the coarse runtime position.
- Use the reset action to return a target to position `50`, i.e. `1x`.
- To make relative-mode right-edge scrolling faster or slower relative to
  hold-to-scroll, adjust `NATIVE_WHEEL_SPEED_BOOST` in the processor code.
- To make absolute-mode pointer movement feel different before processor
  scaling, tune `absolute-relative-divisor`.
- To make absolute-mode edge scrolling feel different before processor scaling,
  tune `absolute-scroll-divisor`.
- Leave `scroll-max-delta = <0>` if fast swipes should be allowed to emit large
  wheel deltas. Set it only when single-event spikes need to be capped.
