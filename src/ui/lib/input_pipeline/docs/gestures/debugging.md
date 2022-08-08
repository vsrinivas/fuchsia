# input_pipeline > Gestures > Debugging

Reviewed-on: 2022-08-05

# Theory

We optimisitically assume that the gesture arena is receiving
clean data from the sensor. And given the unit tests for the
gesture arena, we believe that the gesture arena is routing
events to the various recognizers appropriately.

As such, a misrecognized gesture is most likely due to:

1. A false negative: a recognizer is failing to claim a gesture,
   or the recognizer is ending the gesture prematurely, OR
2. A false positive: a recognizer is errantly claiming a gesture
   that does not truly match the recognizer.

# False negatives

To help debug false negatives, the gesture arena logs the reason
that each recognizer gave for reporting that a gesture did not
match, and the reason that the `gesture_arena::Winner` (if any)
gave for ending the gesture.

# False positives

To help debug false positives, the gesture arena can log the set of
active recognizers ("the contender set") after every
`touch_binding::TouchpadEvent`.

However, because this is chatty, such logging is disabled by default.
To enable these logs, uncomment the call to `log_mutable_state()`
in `gesture_arena.rs`.