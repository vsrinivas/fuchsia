# input_pipeline > Gesture Arena

The gesture arena processes unhandled `touch_binding::TouchpadEvent`s, matching
those events against a set of recognizers for common touchpad gestures (such
as tapping and scrolling).

When a gesture is recognized, the gesture arena emits the corresponding
mouse events downstream.

If the `TouchpadEvent`s do not match any known gesture, the gesture arena
discards them.

More details, see
* [`GestureArena` doc](../gestures/gesture_arena.md)
* [`click` recognizer doc](../gestures/click.md)
* [`motion` recognizer doc](../gestures/motion.md)
* [`one_finger_drag` recognizer doc](../gestures/one_finger_drag.md)
* [`primary_tap` recognizer doc](../gestures/primary_tap.md)
* [`scroll` recognizer doc](../gestures/scroll.md)
* [`secondary_tap` recognizer doc](../gestures/secondary_tap.md)