# input_pipeline > Gestures > Two finger Button Recognizer

Reviewed-on: 2022-11-08

# Purpose

The two finger button recognizer detects a two finger click or a click-drag on
the touchpad.

A click occurs when the user exerts enough force on the touchpad to move the
button under the pad with 2 finger, then release the button. (This is distinct
from a tap, which does not move the pad.)

A two finger drag occurs when the user exerts enough force on the touchpad to
move the pad itself without releasing the button then move the finger more
than threshold.

Notably, this includes discarding any spurious motion that occurs during the
click (when a button has been pressed but not yet released).

# State machine

The two finger button recognizer implements the state machine below.

![recognizer state machine](two_button_state_machine.png)

The state machine is also available in other formats:

- [state machine as graphviz source](two_button_state_machine.dot)
- [state machine as SVG](two_button_state_machine.svg)
