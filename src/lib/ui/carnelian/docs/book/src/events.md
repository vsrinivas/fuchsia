# User Input Events

Carnelian defines its own data structures to represent user input events like mouse and keyboard.
It does this in order to provide the same structures when it is running directly on the display
controller as when it is running under Scenic.

Carnelian provides a
[pointer](https://fuchsia-docs.firebaseapp.com/rust/carnelian/input/pointer/struct.Event.html)
struct, which represents a least-common denominator abstraction of mouse and touch input events.
These pointer events are delivered in favor of mouse or keyboard events unless the [view
assistant](https://fuchsia-docs.firebaseapp.com/rust/carnelian/trait.ViewAssistant.html)
implements the
[`use_pointer_events`](https://fuchsia-docs.firebaseapp.com/rust/carnelian/trait.ViewAssistant.html#method.uses_pointer_events) trait method and returns false.

## Concerns

The unit testing of the conversion of the platform input events to Carnelians depends on static arrays of input event types. If the types change these tests will be hard to update.
