# Dead keys support in [Fuchsia][fx]'s input pipeline

[fx]: https://fuchsia.dev

> **Summary.** This document explains the design of [Fuchsia's dead keys
> support][dks].

[dks]: https://fuchsia-review.googlesource.com/c/fuchsia/+/602043

## Introduction

Dead key is a character composition approach where an accented character,
typically from a Western European alphabet, is composed by actuating two
keys on the keyboard:

1. A "dead key" which determines which diacritic is to be placed on the
   character, and which produces no immediate output; and
2. The character onto which the diacritic is to be placed.

The resulting two successive key actuations produce an effect of single
accented character being emitted.  The dead key handler relies on keymap
already having been applied, and the use of [key meanings][km].  This means
that the dead key handler must be added to the input pipeline after the keymap
handler in the input pipeline.

[km]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.input3/events.fidl;l=54;drc=b4b526858f8408d60e81f49cb518e63a8694e62f

## Dead key handler

The [dead key handler][dkh] can delay or modify the key meanings, but it never
delays nor modifies key events.  This ensures that clients which require key
events see the key events as they come in.  The key meanings may be delayed
because of the delayed effect of composition.

[dkh]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/dead_keys_handler.rs

The state machine of the dead key handler is watching for dead key and "live"
key combinations, and handles all their possible interleaving. The event
sequences vary from the "obvious" ones such as "dead key press and release
followed by a live key press and release", to not so obvious ones such as:
"dead key press and hold, shift press, live key press and hold followed by
another live key press, followed by arbitrary sequence of key releases".

The dead key composition is started by observing a key press that amounts
to a dead key.  The first non-dead key that gets actuated thereafter becomes
the "live" key that we will attempt to add a diacritic to.  When such a live
key is actuated, we will emit a key meaning equivalent to producing an
accented character.

A complication here is that composition can unfold in any number of ways.
The user could press and release the dead key, then press and release
the live key.  The user could, also, press and hold the dead key, then
press any number of live or dead keys in an arbitrary order.

Another complication is that the user could press the dead key twice, which
should also be handled correctly. In this case, "correct" handling implies
emitting the dead key as an accented character.  Similarly, two different
dead keys pressed in succession are handled by (1) emitting the first as
an accented character, and restarting composition with the second. It is
worth noting that the key press and key release events could be arbitrarily
interleaved for the two dead keys, and that should be handled correctly too.

A third complication is that, while all the composition is taking place,
the pipeline must emit the `KeyEvent`s consistent with the key event protocol,
but keep key meanings suppressed until the time that the key meanings have
been resolved by the combination.

The elements of state are as follows:

  * Did we see a live key release event? (bit `d`)
  * Did we see a live key press event? (bit `c`)
  * Did we see a dead key release event? (bit `b`)
  * Did we see a dead key press event? (bit `a`)

While there are 16 total variations of the values `a..d`, and therefore 16
points in the resulting state space, not every variation of the above elements
is possible and allowed. But even the states that ostensibly shouldn't be
possible (e.g. observed a release event before a press) should be accounted for
in order to implement self-correcting behavior if needed.  The `State` enum
below encodes each state as a name `Sdcba`, where each of `a..d` are booleans,
encoded as characters `0` and `1` as conventional. So for example, `S0101` is a
state where we observed a dead key press event, and a live key press event.

> Note: I made an experiment where I tried to use more illustrative state
> names, but the number of variations didn't make the resulting names any more
> meaningful compared to the current state name encoding scheme. So compact
> naming it is.

```rust
#[derive(Debug, Clone)]
enum State {
    /// We have yet to see a key to act on.
    S0000,
    /// We saw an actuation of a dead key.
    S0001 { dead_key_down: StoredEvent },
    /// A dead key was pressed and released.
    S0011 {
        dead_key_down: StoredEvent,
        dead_key_up: StoredEvent,
    },
    /// A dead key was pressed and released, followed by a live key press.
    S0111 {
        dead_key_down: StoredEvent,
        dead_key_up: StoredEvent,
        live_key_down: StoredEvent
    },
    /// A dead key was pressed, followed by a live key press.
    S0101 {
        dead_key_down: StoredEvent,
        live_key_down: StoredEvent,
    },
    /// A dead key was pressed, then a live key was pressed and released.
    S1101 { dead_key_down: StoredEvent },
}
```
