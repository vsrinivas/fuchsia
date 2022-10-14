// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements dead key handling.
//!
//! Dead key is a character composition approach where an accented character,
//! typically from a Western European alphabet, is composed by actuating two
//! keys on the keyboard:
//!
//! 1. A "dead key" which determines which diacritic is to be placed on the
//!    character, and which produces no immediate output; and
//! 2. The character onto which the diacritic is to be placed.
//!
//! The resulting two successive key actuations produce an effect of single
//! accented character being emitted.
//!
//! The dead key handler relies on keymap already having been applied, and the
//! use of key meanings.
//!
//! This means that the dead key handler must be added to the input pipeline
//! after the keymap handler in the input pipeline.
//!
//! The dead key handler can delay or modify the key meanings, but it never delays nor
//! modifies key events.  This ensures that clients which require key events see the
//! key events as they come in.  The key meanings may be delayed because of the delayed
//! effect of composition.
//!
//! The state machine of the dead key handler is watching for dead key and "live" key
//! combinations, and handles all their possible interleaving. The event sequences
//! vary from the "obvious" ones such as "dead key press and release followed
//! by a live key press and release", to not so obvious ones such as: "dead key
//! press and hold, shift press, live key press and hold followed by another
//! live key press, followed by arbitrary sequence of key releases".
//!
//! See the documentation for [Handler] for some more detail.

use crate::input_device::{
    Handled, InputDeviceDescriptor, InputDeviceEvent, InputEvent, UnhandledInputEvent,
};
use crate::input_handler::UnhandledInputHandler;
use crate::keyboard_binding::KeyboardEvent;
use async_trait::async_trait;
use core::fmt;
use fidl_fuchsia_ui_input3::{KeyEventType, KeyMeaning};
use fuchsia_syslog::fx_log_debug;
use fuchsia_zircon as zx;
use rust_icu_sys as usys;
use rust_icu_unorm2 as unorm;
use std::cell::RefCell;
use std::rc::Rc;

// There probably is a more general method of determining whether the characters
// are combining characters. But somehow it escapes me now.
const GRAVE: u32 = 0x300;
const ACUTE: u32 = 0x301;
const CIRCUMFLEX: u32 = 0x302;
const TILDE: u32 = 0x303;

/// Returns true if `c` is one of the dead keys we support.
///
/// This should likely be some ICU library function, but I'm not sure which one.
fn is_dead_key(c: u32) -> bool {
    match c {
        GRAVE | ACUTE | CIRCUMFLEX | TILDE => true,
        _ => false,
    }
}

/// Removes the combining effect from a combining code point, leaving only
/// the diacritic.
///
/// This should likely be some ICU library function, but I'm not sure which one.
fn remove_combination(c: u32) -> u32 {
    match c {
        GRAVE => '`' as u32,
        ACUTE => '\'' as u32,
        CIRCUMFLEX => '^' as u32,
        TILDE => '~' as u32,
        _ => c,
    }
}

/// StoredEvent is an InputEvent which is known to be a keyboard event.
#[derive(Debug, Clone)]
struct StoredEvent {
    event: KeyboardEvent,
    device_descriptor: InputDeviceDescriptor,
    event_time: zx::Time,
}

impl fmt::Display for StoredEvent {
    // Implement a compact [Display], as the device descriptor is not
    // normally very interesting to see.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "event: {:?}, event_time: {:?}", &self.event, &self.event_time)
    }
}

impl Into<InputEvent> for StoredEvent {
    /// Converts [StoredEvent] into [InputEvent].
    fn into(self) -> InputEvent {
        InputEvent {
            device_event: InputDeviceEvent::Keyboard(self.event),
            device_descriptor: self.device_descriptor,
            event_time: self.event_time,
            handled: Handled::No,
            trace_id: None,
        }
    }
}

impl Into<Vec<InputEvent>> for StoredEvent {
    fn into(self) -> Vec<InputEvent> {
        vec![self.into()]
    }
}

/// Whether a [StoredEvent] corresponds to a live key or a dead key.
enum Liveness {
    /// The key is dead.
    Dead,
    /// The key is live.
    Live,
}

/// Whether two events are the same or different by key.
enum Sameness {
    /// Two events are the same by key.
    Same,
    /// Two events are different.
    Other,
}

impl StoredEvent {
    /// Repackages self into a new [StoredEvent], with `event` replaced as supplied.
    fn into_with_event(self, event: KeyboardEvent) -> Self {
        StoredEvent {
            event,
            device_descriptor: self.device_descriptor,
            event_time: self.event_time,
        }
    }

    /// Returns the code point contained in this [StoredEvent].
    fn code_point(&self) -> u32 {
        match self.event.get_key_meaning() {
            Some(KeyMeaning::Codepoint(c)) => c,
            _ => panic!("programming error: requested code point for an event that has none"),
        }
    }

    /// Modifies this [StoredEvent] to contain a new code point instead of whatever was there.
    fn into_with_code_point(self, code_point: u32) -> Self {
        let new_event =
            self.event.clone().into_with_key_meaning(Some(KeyMeaning::Codepoint(code_point)));
        self.into_with_event(new_event)
    }

    /// Returns true if [StoredEvent] contains a valid code point.
    fn is_code_point(&self) -> bool {
        match self.event.get_key_meaning() {
            // Some nonprintable keys have the code point value set to 0.
            Some(KeyMeaning::Codepoint(c)) => c != 0,
            _ => false,
        }
    }

    /// Returns whether the key is a dead key or not.  The return value is an enum
    /// to make the state machine match arms more readable.
    fn key_liveness(&self) -> Liveness {
        match self.event.get_key_meaning() {
            Some(KeyMeaning::Codepoint(c)) if is_dead_key(c) => Liveness::Dead,
            _ => Liveness::Live,
        }
    }

    /// Returns the key event type (pressed, released, or something else)
    fn e_type(&self) -> KeyEventType {
        self.event.get_event_type_folded()
    }

    /// Returns a new [StoredEvent] based on `Self`, but with the combining effect removed.
    fn into_base_character(self) -> Self {
        let key_meaning = self.event.get_key_meaning();
        match key_meaning {
            Some(KeyMeaning::Codepoint(c)) => {
                let new_event = self
                    .event
                    .clone()
                    .into_with_key_meaning(Some(KeyMeaning::Codepoint(remove_combination(c))));
                self.into_with_event(new_event)
            }
            _ => self,
        }
    }

    /// Returns a new [StoredEvent], but with key meaning removed.
    fn remove_key_meaning(self) -> Self {
        let mut event = self.event.clone();
        // A zero code point means a KeyEvent for which its edit effect should
        // be ignored. In contrast, an event with an unset code point has by
        // definition the same effect as if the US QWERTY keymap were applied.
        // See discussion at:
        // https://groups.google.com/a/fuchsia.dev/g/ui-input-dev/c/ITYKvbJS6_o/m/8kK0DRccDAAJ
        event = event.into_with_key_meaning(Some(KeyMeaning::Codepoint(0)));
        self.into_with_event(event)
    }

    /// Returns whether the two keys `this` and `that` are in fact the same key
    /// as per the USB HID usage reported.  The return value is an enum to make
    /// the state machine match arms more readable.
    fn key_sameness(this: &StoredEvent, that: &StoredEvent) -> Sameness {
        match this.event.get_key() == that.event.get_key() {
            true => Sameness::Same,
            false => Sameness::Other,
        }
    }
}

/// State contains the current observed state of the dead key state machine.
///
/// The dead key composition is started by observing a key press that amounts
/// to a dead key.  The first non-dead key that gets actuated thereafter becomes
/// the "live" key that we will attempt to add a diacritic to.  When such a live
/// key is actuated, we will emit a key meaning equivalent to producing an
/// accented character.
///
/// A complication here is that composition can unfold in any number of ways.
/// The user could press and release the dead key, then press and release
/// the live key.  The user could, also, press and hold the dead key, then
/// press any number of live or dead keys in an arbitrary order.
///
/// Another complication is that the user could press the dead key twice, which
/// should also be handled correctly. In this case, "correct" handling implies
/// emitting the dead key as an accented character.  Similarly, two different
/// dead keys pressed in succession are handled by (1) emitting the first as
/// an accented character, and restarting composition with the second. It is
/// worth noting that the key press and key release events could be arbitrarily
/// interleaved for the two dead keys, and that should be handled correctly too.
///
/// A third complication is that, while all the composition is taking place,
/// the pipeline must emit the `KeyEvent`s consistent with the key event protocol,
/// but keep key meanings suppressed until the time that the key meanings have
/// been resolved by the combination.
///
/// The elements of state are as follows:
///
///   * Did we see a dead key press event? (bit `a`)
///   * Did we see a dead key release event? (bit `b`)
///   * Did we see a live key press event? (bit `c`)
///   * Did we see a live key release event? (bit `d`)
///
/// Almost any variation of the above elements is possible and allowed.  Even
/// the states that ostensibly shouldn't be possible (e.g. observed a release
/// event before a press) should be accounted for in order to implement
/// self-correcting behavior if needed.  The [State] enum below encodes each
/// state as a name `Sdcba`, where each of `a..d` are booleans, encoded
/// as characters `0` and `1` as conventional. So for example, `S0101`
/// is a state where we observed a dead key press event, and a live key press
/// event.  I made an experiment where I tried to use more illustrative state
/// names, but the number of variations didn't make the resulting names any more
/// meaningful compared to the current state name encoding scheme. So compact
/// naming it is.
#[derive(Debug, Clone)]
enum State {
    /// We have yet to see a key to act on.
    S0000,

    /// We saw an actuation of a dead key.
    S0001 { dead_key_down: StoredEvent },

    /// A dead key was pressed and released.
    S0011 { dead_key_down: StoredEvent, dead_key_up: StoredEvent },

    /// A dead key was pressed and released, followed by a live key press.
    S0111 { dead_key_down: StoredEvent, dead_key_up: StoredEvent, live_key_down: StoredEvent },

    /// A dead key was pressed, followed by a live key press.
    S0101 { dead_key_down: StoredEvent, live_key_down: StoredEvent },

    /// A dead key was pressed, then a live key was pressed and released.
    S1101 { dead_key_down: StoredEvent },
}

#[derive(Debug)]
pub struct DeadKeysHandler {
    /// Tracks the current state of the dead key composition.
    state: RefCell<State>,

    /// The unicode normalizer used for composition.
    normalizer: unorm::UNormalizer,

    /// This handler requires ICU data to be live. This is ensured by holding
    /// a reference to an ICU data loader.
    _data: icu_data::Loader,
}

/// This trait implementation allows the [Handler] to be hooked up into the input
/// pipeline.
#[async_trait(?Send)]
impl UnhandledInputHandler for DeadKeysHandler {
    async fn handle_unhandled_input_event(
        self: Rc<Self>,
        unhandled_input_event: UnhandledInputEvent,
    ) -> Vec<InputEvent> {
        self.handle_unhandled_input_event_internal(unhandled_input_event)
    }
}

impl DeadKeysHandler {
    /// Creates a new instance of the dead keys handler.
    pub fn new(icu_data: icu_data::Loader) -> Rc<Self> {
        let handler = DeadKeysHandler {
            state: RefCell::new(State::S0000),
            // The NFC normalizer performs the needed composition and is not
            // lossy.
            normalizer: unorm::UNormalizer::new_nfc().unwrap(),
            _data: icu_data,
        };
        Rc::new(handler)
    }

    fn handle_unhandled_input_event_internal(
        self: Rc<Self>,
        unhandled_input_event: UnhandledInputEvent,
    ) -> Vec<InputEvent> {
        match unhandled_input_event {
            UnhandledInputEvent {
                device_event: InputDeviceEvent::Keyboard(event),
                device_descriptor,
                event_time,
                trace_id: _,
            } => {
                let event = StoredEvent { event, device_descriptor, event_time };
                // Separated into two statements to ensure the logs are not truncated.
                fx_log_debug!("state: {:?}", self.state.borrow());
                fx_log_debug!("event: {}", &event);
                let result = self.process_keyboard_event(event);
                fx_log_debug!("result: {:?}", &result);
                result
            }

            // Pass other events unchanged.
            _ => vec![InputEvent::from(unhandled_input_event)],
        }
    }

    /// Sets the internal handler state to `new_state`.
    fn set_state(self: &Rc<Self>, new_state: State) {
        *(self.state.borrow_mut()) = new_state;
    }

    /// Attaches a key meaning to each passing keyboard event.
    ///
    /// Underlying this function is a state machine which registers the flow of dead and live keys
    /// after each reported event, and modifies the input event stream accordingly.  For example,
    /// a sequence of events where a dead key is pressed and released, followed by a live key
    /// press and release, results in a composed character being emitted.  The state machine
    /// takese care of this sequence, but also of other less obvious sequences and their effects.
    fn process_keyboard_event(self: &Rc<Self>, event: StoredEvent) -> Vec<InputEvent> {
        if !event.is_code_point() {
            // Pass through any non-codepoint events.
            return event.into();
        }
        let old_state = self.state.borrow().clone();
        match old_state {
            // We are waiting for the composition to begin.
            State::S0000 => match (event.key_liveness(), event.e_type()) {
                // A dead key press starts composition.  We advance to the next
                // state machine state, and eliminate any key meaning from the
                // key event, since we anticipate its use in composition.
                (Liveness::Dead, KeyEventType::Pressed) => {
                    self.set_state(State::S0001 { dead_key_down: event.clone() });
                    event.remove_key_meaning().into()
                }

                // A dead key release while we're waiting for a dead key press,
                // this is probably a remnant of an earlier double press, remove the
                // combining from it and forward.  Keep waiting for composition
                // to begin.
                (Liveness::Dead, KeyEventType::Released) => event.into_base_character().into(),

                // Any other events can be forwarded unmodified.
                _ => event.into(),
            },

            // We have seen a dead key press, but not release.
            State::S0001 { dead_key_down } => {
                match (
                    event.key_liveness(),
                    StoredEvent::key_sameness(&event, &dead_key_down),
                    event.e_type(),
                ) {
                    // The same dead key that was pressed the other time was released.
                    // Emit a stripped version, and start waiting for a live key.
                    (Liveness::Dead, Sameness::Same, KeyEventType::Released) => {
                        self.set_state(State::S0011 { dead_key_down, dead_key_up: event.clone() });
                        event.remove_key_meaning().into()
                    }

                    // Another dead key was released at this point.  Since
                    // we can not start a new combination here, we must forward
                    // it with meaning stripped.
                    (Liveness::Dead, Sameness::Other, KeyEventType::Released) => {
                        event.remove_key_meaning().into()
                    }

                    // The same dead key was pressed again, while we have seen
                    // it pressed before.  This can happen when autorepeat kicks
                    // in.  We treat this the same as two successive actuations
                    // i.e. we send a stripped version of the character, and
                    // go back to waiting.
                    (Liveness::Dead, Sameness::Same, KeyEventType::Pressed) => {
                        self.set_state(State::S0000);
                        event.into_base_character().into()
                    }

                    // A different dead key was pressed.  This stops the ongoing
                    // composition, and starts a new one with a new dead key.  However,
                    // what we emit is a bit subtle: we emit a key press event
                    // for the *new* key, but with a key meaning of the stripped
                    // version of the current key.
                    (Liveness::Dead, Sameness::Other, KeyEventType::Pressed) => {
                        let current_removed = dead_key_down.clone().into_base_character();
                        self.set_state(State::S0001 { dead_key_down: event.clone() });
                        event.into_with_code_point(current_removed.code_point()).into()
                    }

                    // A live key was pressed while the dead key is held down. Yay!
                    //
                    // Compose and ship out the live key with attached new meaning.
                    //
                    // A very similar piece of code happens in the state `State::S0011`,
                    // except we get there through a different sequence of events.
                    // Please refer to that code for the details about composition.
                    (Liveness::Live, _, KeyEventType::Pressed) => {
                        let maybe_composed = self.normalizer.compose_pair(
                            event.code_point() as usys::UChar32,
                            dead_key_down.code_point() as usys::UChar32,
                        );

                        if maybe_composed >= 0 {
                            // Composition was a success.
                            let composed_event = event.into_with_code_point(maybe_composed as u32);
                            self.set_state(State::S0101 {
                                dead_key_down,
                                live_key_down: composed_event.clone(),
                            });
                            return composed_event.into();
                        } else {
                            // FAIL!
                            self.set_state(State::S0101 {
                                dead_key_down,
                                live_key_down: event.clone(),
                            });
                            return event.into();
                        }
                    }
                    // All other key events are forwarded unmodified.
                    _ => event.into(),
                }
            }

            // The dead key was pressed and released, the first live key that
            // gets pressed after that now will be used for the composition.
            State::S0011 { dead_key_down, dead_key_up } => {
                match (event.key_liveness(), event.e_type()) {
                    // We observed a dead key actuation.
                    (Liveness::Dead, KeyEventType::Pressed) => {
                        match StoredEvent::key_sameness(&dead_key_down, &event) {
                            // The user pressed the same dead key again.  Let's "compose" it by
                            // stripping its diacritic and making that a compose key.
                            Sameness::Same => {
                                let event = event.into_base_character();
                                self.set_state(State::S0111 {
                                    dead_key_down,
                                    dead_key_up,
                                    live_key_down: event.clone(),
                                });
                                event.into()
                            }
                            // The user pressed a different dead key. It would have been nice
                            // to start a new composition, but we can not express that with the
                            // KeyEvent API, since that would require emitting spurious press and
                            // release key events for the dead key press and release.
                            //
                            // Instead, forward the key unmodified and cancel
                            // the composition.  We may revisit this if the KeyEvent API is
                            // changed to allow decoupling key events from key meanings.
                            Sameness::Other => {
                                self.set_state(State::S0000);
                                event.into_base_character().into()
                            }
                        }
                    }

                    // We observed a dead key release.  This is likely a dead key
                    // from the *previous* composition attempt.  Nothing to do here,
                    // except forward it stripped of key meaning.
                    (Liveness::Dead, KeyEventType::Released) => event.remove_key_meaning().into(),

                    // Oh, frabjous day! Someone pressed a live key that may be
                    // possible to combine!  Let's try it out!  If composition is
                    // a success, emit the current key with the meaning set to
                    // the composed character.
                    (Liveness::Live, KeyEventType::Pressed) => {
                        let maybe_composed = self.normalizer.compose_pair(
                            event.code_point() as usys::UChar32,
                            dead_key_down.code_point() as usys::UChar32,
                        );

                        if maybe_composed >= 0 {
                            // Composition was a success.
                            // Emit the composed event, remember it also when
                            // transitioning to S0111, so we can recover the key meaning
                            // when the live key is released.
                            let composed_event = event.into_with_code_point(maybe_composed as u32);
                            self.set_state(State::S0111 {
                                dead_key_down,
                                dead_key_up,
                                live_key_down: composed_event.clone(),
                            });
                            return composed_event.into();
                        } else {
                            fx_log_debug!("compose failed for: {}\n", &event);
                            // FAIL!
                            // Composition failed, what now?  We would need to
                            // emit TWO characters - one for the now-defunct
                            // dead key, and another for the current live key.
                            // But this is not possible, since we may not emit
                            // more combining key events, but must always emit
                            // both the key and the key meaning since that is
                            // how our protocol works.  Well, we reached the
                            // limit of what key event composition may do, so
                            // let's simply agree to emit the current event
                            // unmodified and forget we had the dead key.
                            self.set_state(State::S0111 {
                                dead_key_down,
                                dead_key_up,
                                live_key_down: event.clone(),
                            });
                            return event.into();
                        }
                    }

                    // All other key events are forwarded unmodified.
                    _ => event.into(),
                }
            }

            // We already combined the live key with the dead key, and are
            // now waiting for the live key to be released.
            State::S0111 { dead_key_down, dead_key_up, live_key_down } => {
                match (
                    event.key_liveness(),
                    // Here we compare the current key with the live key down,
                    // unlike in prior states.
                    StoredEvent::key_sameness(&event, &live_key_down),
                    event.e_type(),
                ) {
                    // This is what we've been waiting for: the live key is now
                    // lifted.  Emit the live key release using the same code point
                    // as we used when the key went down, and we're done.
                    (Liveness::Live, Sameness::Same, KeyEventType::Released) => {
                        self.set_state(State::S0000);
                        event.into_with_code_point(live_key_down.code_point()).into()
                    }

                    // A second press of the live key we're combining.  This is
                    // probably a consequence of autorepeat.  The effect should
                    // be to complete the composition and continue emitting the
                    // "base" key meaning for any further repeats; but also
                    // continue waiting for a key release.
                    (Liveness::Live, Sameness::Same, KeyEventType::Pressed) => {
                        let base_codepoint = event.code_point();
                        let combined_event =
                            event.clone().into_with_code_point(live_key_down.code_point());
                        // We emit a combined key, but further repeats will use the
                        // base code point and not combine.
                        self.set_state(State::S0111 {
                            dead_key_down,
                            dead_key_up,
                            live_key_down: event.into_with_code_point(base_codepoint),
                        });
                        combined_event.into()
                    }

                    // If another live key event comes in, just forward it, and
                    // continue waiting for the last live key release.
                    (Liveness::Live, Sameness::Other, _) => event.into(),

                    // Another dead key has been pressed in addition to what
                    // had been pressed before. So now, we are waiting for the
                    // user to release the live key we already composed, but the
                    // user is again pressing a compose key instead.
                    //
                    // Ideally, we'd want to start new composition with the
                    // new dead key.  But, there's still the issue with the
                    // live key that is still being pressed: when it is eventually
                    // released, we want to have it have exactly the same key
                    // meaning as what we emitted for when it was pressed.  But,
                    // that may happen arbitrarily late afterwards, and we'd
                    // prefer not to keep any composition state for that long.
                    //
                    // That suggests that we must not honor this new dead key
                    // as composition.  But, also, we must not drop the key
                    // event on the floor, since the clients that read key
                    // events must receive it.  So, we just *turn* off
                    // the combining effect on this key, forward it like that,
                    // and continue waiting for the key release.
                    (Liveness::Dead, _, KeyEventType::Pressed) => event.remove_key_meaning().into(),

                    (Liveness::Dead, _, KeyEventType::Released) => {
                        match StoredEvent::key_sameness(&event, &live_key_down) {
                            // Special: if the released key a dead key and the same as the
                            // "live" composing key, then we're seeing a release of a doubly-
                            // pressed dead key.  This one needs to be emitted as a diacritic.
                            Sameness::Same => {
                                self.set_state(State::S0000);
                                event.into_base_character().into()
                            }

                            // All other dead keys are forwarded with stripped key meanings.
                            // We have no way to handle them further.
                            Sameness::Other => event.remove_key_meaning().into(),
                        }
                    }

                    // Forward any other events unmodified.
                    _ => event.into(),
                }
            }

            // The user pressed and is holding the dead key; and pressed and
            // is holding a live key.
            State::S0101 { dead_key_down, live_key_down } => {
                match (event.key_liveness(), event.e_type()) {
                    // The same dead key we're already holding is pressed.  Just forward
                    // the key event, but not meaning.
                    (Liveness::Dead, KeyEventType::Pressed) => event.remove_key_meaning().into(),

                    (Liveness::Dead, KeyEventType::Released) => {
                        // The dead key that we are using for combining is released.
                        // Emit its release event without a key meaning and go to a
                        // state that expects a release of the live key.
                        match StoredEvent::key_sameness(&dead_key_down, &event) {
                            Sameness::Same => {
                                self.set_state(State::S0111 {
                                    dead_key_down,
                                    dead_key_up: event.clone(),
                                    live_key_down,
                                });
                                event.remove_key_meaning().into()
                            }

                            // Other dead key is released.  Remove its key meaning, but forward.
                            Sameness::Other => event.remove_key_meaning().into(),
                        }
                    }
                    (Liveness::Live, KeyEventType::Pressed) => {
                        match StoredEvent::key_sameness(&live_key_down, &event) {
                            // The currently pressed live key is pressed again.
                            // This is autorepeat.  We emit one composed key, but any
                            // further emitted keys will not compose.  This
                            // should be similar to `State::S0111`, except the
                            // transition is back to *this* state.
                            Sameness::Same => {
                                let base_codepoint = event.code_point();
                                let combined_event =
                                    event.clone().into_with_code_point(live_key_down.code_point());
                                self.set_state(State::S0101 {
                                    dead_key_down,
                                    live_key_down: event.into_with_code_point(base_codepoint),
                                });
                                combined_event.into()
                            }
                            Sameness::Other => event.into(),
                        }
                    }
                    (Liveness::Live, KeyEventType::Released) => {
                        match StoredEvent::key_sameness(&live_key_down, &event) {
                            Sameness::Same => {
                                self.set_state(State::S1101 { dead_key_down });
                                event.into_with_code_point(live_key_down.code_point()).into()
                            }

                            // Any other release just gets forwarded.
                            Sameness::Other => event.into(),
                        }
                    }

                    // Forward any other events unmodified
                    _ => event.into(),
                }
            }

            // The dead key is still actuated, but we already sent out the
            // combined versions of the live key.
            State::S1101 { dead_key_down } => {
                match (event.key_liveness(), event.e_type()) {
                    (Liveness::Dead, KeyEventType::Pressed) => {
                        // Two possible cases here, but the outcome is the
                        // same:
                        //
                        // The same dead key is pressed again.  Let's not
                        // do any more compositions here.
                        //
                        // A different dead key has been pressed.  We can
                        // not start a new composition while we have not
                        // closed out the current composition.  For this
                        // reason we ignore the other key.
                        //
                        // A real compositioning API would perhaps allow us
                        // to stack compositions on top of each other, but
                        // we will require any such consumers to go talk to
                        // the text editing API instead.
                        event.remove_key_meaning().into()
                    }

                    (Liveness::Dead, KeyEventType::Released) => {
                        match StoredEvent::key_sameness(&dead_key_down, &event) {
                            // The dead key is released, the composition is
                            // done, let's close up shop.
                            Sameness::Same => {
                                self.set_state(State::S0000);
                                event.remove_key_meaning().into()
                            }
                            // A dead key was released, but not the one that we
                            // are combining by.  Forward with the combining
                            // effect stripped.
                            Sameness::Other => event.remove_key_meaning().into(),
                        }
                    }

                    // Any additional live keys, no matter if they are the same
                    // as the one currently being composed, will *not* be composed,
                    // we forward them unmodified as we wait to close off this
                    // composition.
                    //
                    // Forward any other events unmodified.
                    _ => event.into(),
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing_utilities;
    use fidl_fuchsia_input::Key;
    use fidl_fuchsia_ui_input3::{KeyEventType, KeyMeaning};
    use fuchsia_zircon as zx;
    use pretty_assertions::assert_eq;
    use std::convert::TryFrom as _;

    // Creates a new keyboard event for testing.
    fn new_event(
        key: Key,
        event_type: KeyEventType,
        key_meaning: Option<KeyMeaning>,
    ) -> UnhandledInputEvent {
        UnhandledInputEvent::try_from(testing_utilities::create_keyboard_event_with_handled(
            key,
            event_type,
            /*modifiers=*/ None,
            /*event_time*/ zx::Time::ZERO,
            &InputDeviceDescriptor::Fake,
            /*keymap=*/ None,
            key_meaning,
            /*handled=*/ Handled::No,
        ))
        .unwrap()
    }

    // Tests some common keyboard input use cases with dead keys actuation.
    #[test]
    fn test_input_processing() {
        // A zero codepoint is a way to let the consumers know that this key
        // event should have no effect on the edited text; even though its
        // key event may have other effects, such as moving the hero across
        // the screen in a game.
        const ZERO_CP: Option<KeyMeaning> = Some(KeyMeaning::Codepoint(0));

        #[derive(Debug)]
        struct TestCase {
            name: &'static str,
            // The sequence of input events at the input of the dead keys
            // handler.
            inputs: Vec<UnhandledInputEvent>,
            // The expected sequence of input events, after being transformed
            // by the dead keys handler.
            expected: Vec<UnhandledInputEvent>,
        }
        let tests: Vec<TestCase> = vec![
            TestCase {
                name: "passthrough",
                inputs: vec![
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                ],
                expected: vec![
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                ],
            },
            TestCase {
                name: "A circumflex - dead key first, then live key",
                inputs: vec![
                    new_event(
                        Key::Key5,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::Key5,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                ],
                expected: vec![
                    new_event(Key::Key5, KeyEventType::Pressed, ZERO_CP),
                    new_event(Key::Key5, KeyEventType::Released, ZERO_CP),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                ],
            },
            TestCase {
                name: "A circumflex - dead key held all the way through composition",
                inputs: vec![
                    new_event(
                        Key::Key5,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::Key5,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                ],
                expected: vec![
                    new_event(Key::Key5, KeyEventType::Pressed, ZERO_CP),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                    new_event(Key::Key5, KeyEventType::Released, ZERO_CP),
                ],
            },
            TestCase {
                name: "A circumflex - dead key held until the live key was down",
                inputs: vec![
                    new_event(
                        Key::Key5,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::Key5,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                ],
                expected: vec![
                    new_event(Key::Key5, KeyEventType::Pressed, ZERO_CP),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                    new_event(Key::Key5, KeyEventType::Released, ZERO_CP),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                ],
            },
            TestCase {
                name: "Combining character pressed twice - results in a single diacritic",
                inputs: vec![
                    new_event(
                        Key::Key5,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::Key5,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::Key5,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::Key5,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                ],
                expected: vec![
                    new_event(Key::Key5, KeyEventType::Pressed, ZERO_CP),
                    new_event(Key::Key5, KeyEventType::Released, ZERO_CP),
                    new_event(
                        Key::Key5,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('^' as u32)),
                    ),
                    new_event(
                        Key::Key5,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('^' as u32)),
                    ),
                ],
            },
            TestCase {
                name: "A circumflex - dead key spans live key",
                inputs: vec![
                    new_event(
                        Key::Key5,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::Key5,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                ],
                expected: vec![
                    new_event(Key::Key5, KeyEventType::Pressed, ZERO_CP),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                    new_event(Key::Key5, KeyEventType::Released, ZERO_CP),
                ],
            },
            TestCase {
                name: "Only the first key after the dead key actuation is composed",
                inputs: vec![
                    new_event(
                        Key::Key5,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::Key5,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::E,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('E' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::E,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('E' as u32)),
                    ),
                ],
                expected: vec![
                    new_event(Key::Key5, KeyEventType::Pressed, ZERO_CP),
                    new_event(Key::Key5, KeyEventType::Released, ZERO_CP),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                    new_event(
                        Key::E,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('E' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                    new_event(
                        Key::E,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('E' as u32)),
                    ),
                ],
            },
            TestCase {
                name: "Modifier keys are not affected",
                inputs: vec![
                    new_event(
                        Key::Key5,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::Key5,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(Key::LeftShift, KeyEventType::Pressed, ZERO_CP),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(Key::LeftShift, KeyEventType::Released, ZERO_CP),
                ],
                expected: vec![
                    new_event(Key::Key5, KeyEventType::Pressed, ZERO_CP),
                    new_event(Key::Key5, KeyEventType::Released, ZERO_CP),
                    new_event(Key::LeftShift, KeyEventType::Pressed, ZERO_CP),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                    new_event(Key::LeftShift, KeyEventType::Released, ZERO_CP),
                ],
            },
            TestCase {
                name: "Two dead keys in succession - no compose",
                inputs: vec![
                    new_event(
                        Key::Key5,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::Key5,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(GRAVE as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(GRAVE as u32)),
                    ),
                ],
                expected: vec![
                    new_event(Key::Key5, KeyEventType::Pressed, ZERO_CP),
                    new_event(Key::Key5, KeyEventType::Released, ZERO_CP),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('`' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('`' as u32)),
                    ),
                ],
            },
            TestCase {
                name: "Compose with capital letter",
                inputs: vec![
                    new_event(
                        Key::Key5,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::Key5,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(CIRCUMFLEX as u32)),
                    ),
                    new_event(
                        Key::LeftShift,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(0)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                    ),
                    new_event(
                        Key::LeftShift,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(0)),
                    ),
                ],
                expected: vec![
                    new_event(Key::Key5, KeyEventType::Pressed, ZERO_CP),
                    new_event(Key::Key5, KeyEventType::Released, ZERO_CP),
                    new_event(
                        Key::LeftShift,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(0)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('Â' as u32)),
                    ),
                    new_event(
                        Key::LeftShift,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(0)),
                    ),
                ],
            },
        ];

        let loader = icu_data::Loader::new().unwrap();
        let handler = super::DeadKeysHandler::new(loader);
        for test in tests {
            let actuals: Vec<InputEvent> = test
                .inputs
                .into_iter()
                .map(|event| handler.clone().handle_unhandled_input_event_internal(event))
                .flatten()
                .collect();
            assert_eq!(
                test.expected.into_iter().map(InputEvent::from).collect::<Vec<_>>(),
                actuals,
                "in test: {}",
                test.name
            );
        }
    }
}
