# input_pipeline > Gestures > Gesture Arena

Reviewed-on: 2022-06-14

# Purpose

The gesture arena uses a set of recognizers to interpret incoming
`TouchpadEvent`s, and forward corresponding `MouseEvent`s downstream.

# Roles

* Gesture arena: provides a framework for a set of cooperating recognizers
  to classify touchpad events as a specific gesture

* Recognizer: a Rust module that receives touchpad events from the gesture
  arena, and interfaces with the arena using the
  [TypeState pattern][typestate-pattern]

# Assumptions

The recognizers must be cooperative. The gesture arena does not, for example,
provide any safeguard against a recognizer claiming to match every incoming
`TouchpadEvent`.

# Gesture recognition mechanics

The stream of incoming touchpad events is broken into a series of contests.

At the start of each contest, the gesture arena initializes a `Contender`
for each recognizer, passing `recognizer::Contender::new()` any parameters
relevant to that recognizer.

For example:
* `motion::Contender::new()` should accept an argument which specifies
  the spurious motion threshold.
* `one_finger_tap::Contender::new()` should accept an argument which specifies
  the tap timeout.

After initializing all of the `Contender`s, the gesture arena calls
`examine_event()` on every `Contender`. Recognizers can:
* Bow out by returning `ExamineResult::Mismatch`, OR
* Ask to continue receiving events, by returning
  * `ExamineResult::Contender`, OR
  * `ExamineResult::MatchedContender`

If the recognizer returns a `MatchedContender`, the gesture arena MAY
immediately declare the recognizer the contest winner, and ask the recognizer
to start forwarding events downstream.

Hence, a recognizer SHOULD NOT return a `MatchedContender` until the recognizer
has received all the preliminary events before the recognizer would forward
events downstream.

For example:
* the motion recognizer should not declare a match until the recognizer has
  received events which surpass the spurious motion threshold
* the one-finger-tap recognizer should not declare a match until the recognizer
  has seen the finger removed from the touchpad

The gesture arena continues sending events to `Contender`s and
`MatchedContender`s until:
* there are no more contestants (all `Contender`s and `MatchedContender`s have
  bowed out)
* exactly one `MatchContender` remains

Until the contest ends:
* Recognizers CAN NOT forward events downstream (they are not provided an
  interface to do so).
* The gesture arena accumulates the events into a buffer.

When the contest ends, the gesture arena either
* if all recognizers bowed out: discards all the buffered events
* otherwise:
  1. passes ownership of the buffered events to the `Winner` object from
     the winning recognizer, accepting in response `MouseEvent`s to be
     propagated downstream
  2. passes future events to the `Winner` object from the winning recognizer,
     and accepts from the recognizer `MouseEvent`s to be propagated downstream,
     continuing until the recognizer declares that the gesture has ended

When the `Winner` declares that the gesture has ended, the gesture arena
discards the recognizer (note that all other recognizers have already
been discarded at this point), and starts a new contest.

The new contest may begin immediately, if the `Winner` did not consume the
last event. This might happen, for example, if the user swipes across the
touchpad, then presses the button down.

In this case, the motion recognizer (specifically: `motion::Winner`) would
report the button down as unconsumed, so that the button down can be
interpreted by the one-finger-drag or two-finger-drag recognizers.

If the `Winner` consumes the event when declaring the gesture complete,
then the gesture arena waits for a new event before starting a new contest.

# Notes

When converting `TouchpadEvent`s to `MouseEvent`s, recognizers SHOULD copy the
timestamp unmodified. This is because some downstream consumers (such as
`PointerMotionSensorScaleHandler`, and potentially FIDL clients of Scenic)
require velocity data to carry out their duties.

[typestate-pattern]: http://cliffle.com/blog/rust-typestate/