# input_pipeline > Gestures > One Finger Drag Recognizer

Reviewed-on: 2022-07-20

# Purpose

The one finger drag recognizer detects 1 finger drag on the touchpad, and converts all related
touchpad events to mouse events.

A one finger drag occurs when the user places 1 finger on the touchpad surface and add pressure
to trigger the button then move the finger.

# State machine

The one finger drag recognizer implements the state machine below.

![recognizer state machine](one_finger_drag_state_machine.png)

The state machine is also available in other formats:

- [state machine as graphviz source](one_finger_drag_state_machine.dot)
- [state machine as SVG](one_finger_drag_state_machine.svg)
