# input_pipeline > Gestures > Scroll Recognizer

Reviewed-on: 2022-07-20

# Purpose

The scroll recognizer detects 2 finger swipe on the touchpad, and converts all related
touchpad events to mouse wheel events.

A scroll occurs when the user places 2 fingers on the touchpad surface. And 2 fingers move
approximately horizontal or vertical.

# State machine

The scroll recognizer implements the state machine below.

![recognizer state machine](scroll_state_machine.png)

The state machine is also available in other formats:

- [state machine as graphviz source](scroll_state_machine.dot)
- [state machine as SVG](scroll_state_machine.svg)

The scroll recognizer tolerate a certain degree of angle in the direction.

![direction tolerance](scroll_direction.png)
