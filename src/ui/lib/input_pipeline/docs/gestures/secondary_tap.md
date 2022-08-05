# input_pipeline > Gestures > Secondary Tap Recognizer

Reviewed-on: 2022-07-18

# Purpose

The secondary tap recognizer detects a two-finger-tap on the touchpad, generates a mouse secondary click event, and discards all touchpad events related to the tap.

A secondary tap occurs when the user puts two fingers down on the touchpad, exercises zero-to-minimal movement of both fingers, and then removes both fingers, all within a short time frame and without moving the pad. (This is distinct from a click, for which a user exerts enough force on the touchpad to move the pad itself.)

Notably, this includes discarding any spurious motion that occurs during the secondary tap (when two fingers have made contact down but have not yet been raised up).

# State machine

The secondary tap recognizer implements the state machine below.

![recognizer state machine](secondary_tap_state_machine.png)

The state machine is also available in other formats:

- [state machine as graphviz source](secondary_tap_state_machine.dot)
- [state machine as SVG](secondary_tap_state_machine.svg)
