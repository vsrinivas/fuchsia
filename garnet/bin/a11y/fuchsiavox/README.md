# Fuchsiavox

This is a basic implementation of the Fuchsiavox a11y service for Fuchsia. Its a work
in progress with the current capabilities:
- Single tap/slide one finger to read text under finger and set a11y focus.
- Double tap to perform a tap action.
- Use two finger touch to simulate using one finger regularly.

Requires connections to the a11y manager and a11y touch dispatcher service to function.

Currently inactive due to work for SCN-1124.

## Gesture Detection State Diagram

![Gesture State Diagram](fuchsiavox_gesture_state_machine.png)
