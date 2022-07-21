// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{input_device, input_handler::UnhandledInputHandler, mouse_binding, touch_binding},
    async_trait::async_trait,
    core::cell::RefCell,
    fidl_fuchsia_input_report as fidl_input_report,
    fuchsia_syslog::fx_log_debug,
    fuchsia_zircon as zx,
    std::any::Any,
    std::fmt::Debug,
};

pub(super) const PRIMARY_BUTTON: mouse_binding::MouseButton = 1;
pub(super) const SECONDARY_BUTTON: mouse_binding::MouseButton = 2;

// TODO(https://fxbug.dev/102654): check that we've removed all leading `_` from types
// and variables in this file.
#[derive(Debug, Clone, PartialEq)]
pub(super) struct TouchpadEvent {
    pub(super) timestamp: zx::Time,
    // TODO(https://fxbug.dev/102655): replace these fields with a field that embeds
    // `touch_data: super::touch_binding::TouchpadEvent`.
    pub(super) pressed_buttons: Vec<u8>,
    pub(super) contacts: Vec<touch_binding::TouchContact>,
}

#[derive(Debug, PartialEq)]
pub(super) struct MouseEvent {
    pub(super) timestamp: zx::Time,
    pub(super) mouse_data: mouse_binding::MouseEvent,
}

#[derive(Debug)]
pub(super) enum ExamineEventResult {
    Contender(Box<dyn Contender>),
    MatchedContender(Box<dyn MatchedContender>),
    Mismatch,
}

pub(super) trait Contender: std::fmt::Debug + AsAny {
    /// Examines `event`, to determine whether or not the gesture
    /// is relevant to this `Recognizer`.
    ///
    /// Returns
    /// * `ExamineEventResult::MatchedContender` if this recognizer wants
    ///   to send (or start sending) events downstream, OR
    /// * `ExamineEventResult::Contender` if this recognizer is not yet
    ///   ready to send events downstream, but wants to continue
    ///   contending for the gesture, OR
    /// * `ExamineEventResult::Mismatch` if this recognizer no longer
    ///   wants to contend for this gesture
    fn examine_event(self: Box<Self>, event: &TouchpadEvent) -> ExamineEventResult;
}

pub trait AsAny {
    fn as_any(&self) -> &dyn Any;
}

impl<T: Any> AsAny for T {
    fn as_any(&self) -> &dyn Any {
        self
    }
}

#[derive(Debug)]
pub(super) enum VerifyEventResult {
    MatchedContender(Box<dyn MatchedContender>),
    Mismatch,
}

#[derive(Debug, PartialEq)]
pub(super) enum RecognizedGesture {
    /// Contains one variant for each recognizer, and the
    /// special value `Unrecognized` for when no recognizer
    /// claims the gesture.
    _Unrecognized,
    Click,
    #[allow(dead_code)] // Only used in tests until we implement palm recognizer.
    Palm,
    PrimaryTap,
    Motion,
}

#[derive(Debug)]
pub(super) struct ProcessBufferedEventsResult {
    #[allow(dead_code)] // Unread until we implement the gesture arena.
    pub(super) generated_events: Vec<MouseEvent>,
    #[allow(dead_code)] // Unread until we implement the gesture arena.
    pub(super) winner: Option<Box<dyn Winner>>,
    #[allow(dead_code)] // Unread until we implement the gesture arena.
    pub(super) recognized_gesture: RecognizedGesture, // for latency breakdown
}

pub(super) trait MatchedContender: std::fmt::Debug + AsAny {
    /// Verifies that `event` still matches the gesture that is relevant
    /// to this `Recognizer`.
    ///
    /// Returns
    /// * `VerifyEventResult::MatchedContender` if this recognizer wants
    ///   to send (or start sending) events downstream, OR
    /// * `VerifyEventResult::Mismatch` if this recognizer no longer
    ///   wants to contend for this gesture
    fn verify_event(self: Box<Self>, event: &TouchpadEvent) -> VerifyEventResult;

    /// Takes `events`, and generates corresponding `MouseEvent`s.
    ///
    /// Returns `ProcessBufferedEventsResult` with fields:
    /// * `generated_events`: the sequence of `MouseEvent`s needed
    ///   to effect the gesture downstream.
    /// * `winner`:
    ///   * `None` if the gesture is complete
    ///   * `Some` otherwise
    ///
    /// Note:
    /// * `generated_events` MAY be empty; for example, a palm
    ///   recognizer wants to discard unintended events
    /// * `events` is guaranteed to contains exactly the sequence of
    ///   `TouchpadEvent`s that this recognizer has already examined
    ///   and verified.
    /// * recognizers MAY choose to ignore `events`
    ///   e.g.:
    ///   * a one-finger-tap recognizer does not need to inspect
    ///     `events` to generate the `MouseEvent`s for the button click
    ///   * a motion recognizer, in contrast, needs the details in
    ///     `events` to generate the appropriate motion
    fn process_buffered_events(
        self: Box<Self>,
        events: Vec<TouchpadEvent>,
    ) -> ProcessBufferedEventsResult;
}

#[derive(Debug)]
pub(super) enum ProcessNewEventResult {
    ContinueGesture(Option<MouseEvent>, Box<dyn Winner>),
    EndGesture(Option<TouchpadEvent>),
}

pub(super) trait Winner: std::fmt::Debug {
    /// Takes `event`, and generates corresponding `MouseEvent`s.
    ///
    /// Returns:
    /// * `ContinueGesture(Some, …)` if the gesture is still
    ///   in progress, and a `MouseEvent` should be sent downstream;
    ///   might be used, e.g., by a motion recognizer
    /// * `ContinueGesutre(None, …)` if the gesture is still
    ///   in progress, and no `MouseEvent` should be sent downstream;
    ///   might be used, e.g., by a palm recognizer
    /// * `EndGesture(Some)` if the gesture has ended because
    ///   `event` did not match; might be used, e.g., if the user
    ///    presses the touchpad down after a motion gesture
    /// * `EndGesture(None)` if `event` matches a normal end
    ///   of the gesture; might be used, e.g., if the user lifts
    ///   their finger off the touchpad after a motion gesture
    fn process_new_event(self: Box<Self>, event: TouchpadEvent) -> ProcessNewEventResult;
}

#[derive(Debug)]
enum MutableState {
    /// Not currently processing a gesture event stream.
    Idle,

    /// Processing an event stream to identify the matching gesture.
    Matching {
        contenders: Vec<Box<dyn Contender>>,
        matched_contenders: Vec<Box<dyn MatchedContender>>,
        buffered_events: Vec<TouchpadEvent>,
    },

    #[allow(dead_code)]
    /// The matching gesture has been identified, and is still in progress.
    Forwarding { winner: Box<dyn Winner> },

    /// A transient state during the processing of a single `InputEvent`.
    Invalid,
}

/// Names for the `MutableState`s. Exists to support the state machine debug log.
#[derive(Debug, PartialEq)]
enum StateName {
    Idle,
    Matching,
    Forwarding,
    Invalid,
}

struct _GestureArena<ContenderFactory: Fn() -> Vec<Box<dyn Contender>>> {
    contender_factory: ContenderFactory,
    mutable_state: RefCell<MutableState>,
}

impl<ContenderFactory: Fn() -> Vec<Box<dyn Contender>>> _GestureArena<ContenderFactory> {
    #[cfg(test)]
    fn new_for_test(contender_factory: ContenderFactory) -> _GestureArena<ContenderFactory> {
        _GestureArena { contender_factory, mutable_state: RefCell::new(MutableState::Idle) }
    }
}

impl MutableState {
    fn get_state_name(&self) -> StateName {
        match self {
            Self::Idle => StateName::Idle,
            Self::Matching { .. } => StateName::Matching,
            Self::Forwarding { .. } => StateName::Forwarding,
            Self::Invalid => StateName::Invalid,
        }
    }
}

impl std::convert::TryFrom<input_device::UnhandledInputEvent> for TouchpadEvent {
    type Error = input_device::UnhandledInputEvent;
    fn try_from(
        unhandled_input_event: input_device::UnhandledInputEvent,
    ) -> Result<TouchpadEvent, Self::Error> {
        match unhandled_input_event {
            input_device::UnhandledInputEvent {
                device_event: input_device::InputDeviceEvent::Touchpad(touchpad_data),
                event_time,
                ..
            } => Ok(TouchpadEvent {
                timestamp: event_time,
                pressed_buttons: touchpad_data.pressed_buttons.into_iter().collect::<Vec<_>>(),
                contacts: touchpad_data.injector_contacts,
            }),
            _ => Err(unhandled_input_event),
        }
    }
}

impl std::convert::From<MouseEvent> for input_device::InputEvent {
    fn from(mouse_event: MouseEvent) -> input_device::InputEvent {
        input_device::InputEvent {
            // TODO(https://fxbug.dev/104890): Convert `mouse_event.mouse_data.wheel_delta_v`
            // and `mouse_event.mouse_data.wheel_delta_h` from micrometers to counts.
            device_event: input_device::InputDeviceEvent::Mouse(mouse_event.mouse_data),
            device_descriptor: input_device::InputDeviceDescriptor::Mouse(
                mouse_binding::MouseDeviceDescriptor {
                    // Use a large number for the `device_id` for the gesture arena's
                    // virtual mouse, to avoid conflicts with real devices (which start
                    // at ID 0).
                    //
                    // However, don't use u32::MAX, since the `InputDeviceRegistry`
                    // FIDL handling code uses that for the first registered device
                    // (and works downwards).
                    device_id: u32::MAX / 2,
                    absolute_x_range: Some(fidl_input_report::Range { min: -127, max: 127 }),
                    absolute_y_range: Some(fidl_input_report::Range { min: -127, max: 127 }),
                    wheel_v_range: Some(fidl_input_report::Axis {
                        // Set range based on the range of `position_y` values for the
                        // Atlas touchpad. The touchpad is 2.48 inches tall, and the
                        // position is reported in micrometers. That gives `position_y`
                        // a range of (0, 62992).
                        //
                        // Note that this range is likely larger than needed, since users
                        // probably won't swipe all the way up/down the pad in a single
                        // sampling interval (8msec).
                        //
                        // TODO(https://fxbug.dev/104890): Adjust this range to reflect
                        // the range in counts instead of micrometers.
                        range: fidl_input_report::Range { min: -65535, max: 65535 },
                        unit: fidl_input_report::Unit {
                            type_: fidl_fuchsia_input_report::UnitType::Other,
                            exponent: 0,
                        },
                    }),
                    wheel_h_range: None,
                    buttons: Some(vec![PRIMARY_BUTTON, SECONDARY_BUTTON]),
                    counts_per_mm: mouse_binding::DEFAULT_COUNTS_PER_MM,
                },
            ),
            event_time: mouse_event.timestamp,
            handled: input_device::Handled::No,
            trace_id: None,
        }
    }
}

impl<ContenderFactory: Fn() -> Vec<Box<dyn Contender>>> _GestureArena<ContenderFactory> {
    fn handle_event_while_idle(&self, new_event: TouchpadEvent) -> (MutableState, Vec<MouseEvent>) {
        let (contenders, matched_contenders) = (self.contender_factory)()
            .into_iter()
            .map(|contender| contender.examine_event(&new_event))
            .fold((vec![], vec![]), |(mut contenders, mut matched_contenders), examine_result| {
                match examine_result {
                    ExamineEventResult::Contender(c) => {
                        contenders.push(c);
                        (contenders, matched_contenders)
                    }
                    ExamineEventResult::MatchedContender(m) => {
                        matched_contenders.push(m);
                        (contenders, matched_contenders)
                    }
                    ExamineEventResult::Mismatch => (contenders, matched_contenders),
                }
            });
        if contenders.is_empty() && matched_contenders.is_empty() {
            // No `Contender`s or `MatchedContender`s. The event does not
            // match the start of any gesture:
            // * Remain in the Idle state.
            // * Drop `new_event`, since we have no way to convert that event
            //   into something clients downstream of the input pipeline can
            //   understand.
            (MutableState::Idle, vec![])
        } else {
            // At least one `Contender` or `MatchedContender`. Continue the contest.
            (
                MutableState::Matching {
                    contenders,
                    matched_contenders,
                    buffered_events: vec![new_event],
                },
                vec![],
            )
        }
    }

    fn handle_event_while_matching(
        &self,
        contenders: Vec<Box<dyn Contender>>,
        matched_contenders: Vec<Box<dyn MatchedContender>>,
        buffered_events: Vec<TouchpadEvent>,
        new_event: TouchpadEvent,
    ) -> (MutableState, Vec<MouseEvent>) {
        // Note: This function processes `contenders` after `matched_contenders` to ensure
        // that a recognizer doesn't get invoked twice with the same event. Otherwise, it
        // would be possible for the following sequence of events to occur:
        //
        // 1. This function calls some_recognizer::Contender.examine_event(&new_event).
        // 2. The call returns some_recognizer::MatchedContender.
        // 3. This function calls some_recognizer::MatchedContender.verify_event(&new_event).
        //
        // See also: `does_not_repeat_event_to_matched_contender_returned_by_examine_event` test.
        let matched_contenders = matched_contenders
            .into_iter()
            .map(|matched_contender| matched_contender.verify_event(&new_event))
            .fold(vec![], |mut matched_contenders, verify_result| match verify_result {
                VerifyEventResult::MatchedContender(m) => {
                    matched_contenders.push(m);
                    matched_contenders
                }
                VerifyEventResult::Mismatch => matched_contenders,
            });
        let (contenders, matched_contenders) =
            contenders.into_iter().map(|contender| contender.examine_event(&new_event)).fold(
                (vec![], matched_contenders),
                |(mut contenders, mut matched_contenders), examine_result| match examine_result {
                    ExamineEventResult::Contender(c) => {
                        contenders.push(c);
                        (contenders, matched_contenders)
                    }
                    ExamineEventResult::MatchedContender(m) => {
                        matched_contenders.push(m);
                        (contenders, matched_contenders)
                    }
                    ExamineEventResult::Mismatch => (contenders, matched_contenders),
                },
            );
        match (
            u8::try_from(contenders.len()).unwrap_or(u8::MAX),
            u8::try_from(matched_contenders.len()).unwrap_or(u8::MAX),
        ) {
            // No `Contender`s or `MatchedContender`s. The contest has ended without
            // identifying a gesture.
            (0, 0) => (MutableState::Idle, vec![]),
            // No `Contender`s, and exactly one `MatchedContender`. The contest has ended
            // with a recognized gesture.
            (0, 1) => {
                let mut buffered_events = buffered_events;
                buffered_events.push(new_event);

                let mut matched_contenders = matched_contenders;
                let ProcessBufferedEventsResult { generated_events, winner, recognized_gesture } =
                    matched_contenders.remove(0).process_buffered_events(buffered_events);
                fx_log_debug!("recognized {:?}", recognized_gesture);

                match winner {
                    Some(winner) => (MutableState::Forwarding { winner }, generated_events),
                    None => (MutableState::Idle, generated_events),
                }
            }
            // At least 1 `Contender`, or 2 `MatchedContender`s; continue the contest.
            (1.., _) | (_, 2..) => {
                let mut buffered_events = buffered_events;
                buffered_events.push(new_event);
                (MutableState::Matching { contenders, matched_contenders, buffered_events }, vec![])
            }
        }
    }

    fn handle_event_while_forwarding(
        &self,
        winner: Box<dyn Winner>,
        new_event: TouchpadEvent,
    ) -> (MutableState, Vec<MouseEvent>) {
        match winner.process_new_event(new_event) {
            ProcessNewEventResult::ContinueGesture(generated_event, winner) => {
                (MutableState::Forwarding { winner }, generated_event.into_iter().collect())
            }
            ProcessNewEventResult::EndGesture(Some(unconsumed_event)) => {
                self.handle_event_while_idle(unconsumed_event)
            }
            ProcessNewEventResult::EndGesture(None) => (MutableState::Idle, vec![]),
        }
    }
}

#[async_trait(?Send)]
impl<ContenderFactory: Fn() -> Vec<Box<dyn Contender>>> UnhandledInputHandler
    for _GestureArena<ContenderFactory>
{
    /// Interprets `TouchpadEvent`s, and sends corresponding `MouseEvent`s downstream.
    async fn handle_unhandled_input_event(
        self: std::rc::Rc<Self>,
        unhandled_input_event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent> {
        let touchpad_event = match TouchpadEvent::try_from(unhandled_input_event) {
            Ok(touchpad_event) => touchpad_event,
            Err(other_input_event) => {
                return vec![input_device::InputEvent::from(other_input_event)]
            }
        };
        let old_state_name = self.mutable_state.borrow().get_state_name();
        let (new_state, generated_events) = match self.mutable_state.replace(MutableState::Invalid)
        {
            MutableState::Idle => self.handle_event_while_idle(touchpad_event),
            MutableState::Matching { contenders, matched_contenders, buffered_events } => self
                .handle_event_while_matching(
                    contenders,
                    matched_contenders,
                    buffered_events,
                    touchpad_event,
                ),
            MutableState::Forwarding { winner } => {
                self.handle_event_while_forwarding(winner, touchpad_event)
            }
            MutableState::Invalid => {
                unreachable!();
            }
        };
        fx_log_debug!("gesture_arena: {:?} -> {:?}", old_state_name, new_state.get_state_name());
        self.mutable_state.replace(new_state);
        generated_events.into_iter().map(input_device::InputEvent::from).collect()
    }
}

#[cfg(test)]
mod tests {
    mod utils {
        use {
            super::super::{
                Contender, ExamineEventResult, MatchedContender, ProcessBufferedEventsResult,
                ProcessNewEventResult, TouchpadEvent, VerifyEventResult, Winner, PRIMARY_BUTTON,
            },
            crate::{input_device, mouse_binding, touch_binding, Position},
            assert_matches::assert_matches,
            fidl_fuchsia_input_report as fidl_input_report, fuchsia_zircon as zx,
            maplit::hashset,
            std::{
                cell::{Cell, RefCell},
                rc::Rc,
            },
        };

        /// The gesture arena is mostly agnostic to the event details. Consequently, most
        /// tests can use the same lightly populated touchpad event.
        pub(super) fn make_unhandled_touchpad_event() -> input_device::UnhandledInputEvent {
            input_device::UnhandledInputEvent {
                device_event: input_device::InputDeviceEvent::Touchpad(
                    touch_binding::TouchpadEvent {
                        injector_contacts: vec![],
                        pressed_buttons: hashset! {},
                    },
                ),
                device_descriptor: make_touchpad_descriptor(),
                event_time: zx::Time::ZERO,
                trace_id: None,
            }
        }

        /// The gesture arena is mostly agnostic to the event details. Consequently, most
        /// tests can use the same lightly populated mouse event.
        pub(super) fn make_unhandled_mouse_event() -> input_device::UnhandledInputEvent {
            input_device::UnhandledInputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                    location: mouse_binding::MouseLocation::Relative(
                        mouse_binding::RelativeLocation {
                            counts: Position::zero(),
                            millimeters: Position::zero(),
                        },
                    ),
                    wheel_delta_h: None,
                    wheel_delta_v: None,
                    phase: mouse_binding::MousePhase::Move,
                    affected_buttons: hashset! {},
                    pressed_buttons: hashset! {},
                }),
                device_descriptor: make_mouse_descriptor(),
                event_time: zx::Time::ZERO,
                trace_id: None,
            }
        }

        pub(super) fn make_touchpad_descriptor() -> input_device::InputDeviceDescriptor {
            input_device::InputDeviceDescriptor::Touchpad(touch_binding::TouchpadDeviceDescriptor {
                device_id: 1,
                contacts: vec![touch_binding::ContactDeviceDescriptor {
                    x_range: fidl_input_report::Range { min: 0, max: 10_000 },
                    y_range: fidl_input_report::Range { min: 0, max: 10_000 },
                    pressure_range: None,
                    width_range: Some(fidl_input_report::Range { min: 0, max: 10_000 }),
                    height_range: Some(fidl_input_report::Range { min: 0, max: 10_000 }),
                }],
            })
        }

        pub(super) fn make_mouse_descriptor() -> input_device::InputDeviceDescriptor {
            input_device::InputDeviceDescriptor::Mouse(mouse_binding::MouseDeviceDescriptor {
                device_id: 2,
                absolute_x_range: Some(fidl_input_report::Range { min: -127, max: 127 }),
                absolute_y_range: Some(fidl_input_report::Range { min: -127, max: 127 }),
                wheel_v_range: None,
                wheel_h_range: None,
                buttons: Some(vec![PRIMARY_BUTTON]),
                counts_per_mm: mouse_binding::DEFAULT_COUNTS_PER_MM,
            })
        }

        #[derive(Clone, Debug)]
        /// Provides the ability to
        ///
        /// 1. Plumb a fake `Contender` into a `GestureArena`
        /// 2. Fake interactions between the two
        /// 3. Inspect interactions between the two
        ///
        /// To plumb the fake, pass a `Clone` of the `StubContender` to
        /// `ContenderFactoryOnce`. To fake or inspect interactions, call the
        /// inherent methods on the struct.
        pub(super) struct StubContender {
            inner: Rc<RefCell<StubContenderInner>>,
        }

        impl StubContender {
            pub(super) fn new() -> Self {
                Self {
                    inner: Rc::new(RefCell::new(StubContenderInner {
                        next_result: None,
                        calls_received: 0,
                    })),
                }
            }

            /// Set the value to be returned on the next call to `examine_event()`.
            /// Aborts if a value is already set, since that suggests that a previously
            /// expected call was never made.
            pub(super) fn set_next_result(&self, next_result: ExamineEventResult) {
                self.assert_next_result_is_none();
                self.inner.borrow_mut().next_result = Some(next_result);
            }

            pub(super) fn assert_next_result_is_none(&self) {
                assert_matches!(self.inner.borrow().next_result, None);
            }

            pub(super) fn calls_received(&self) -> usize {
                self.inner.borrow().calls_received
            }

            pub(super) fn ref_count(&self) -> usize {
                Rc::strong_count(&self.inner)
            }
        }

        #[derive(Debug)]
        struct StubContenderInner {
            next_result: Option<ExamineEventResult>,
            calls_received: usize,
        }

        /// A factory that returns `Vec<Box<dyn Contender>>` from `contenders` on the
        /// first call, and `panic()`-s on the second call.
        ///
        /// Useful because
        /// a) `GestureArena` requires that the factory be invocable multiple times, BUT
        /// b) most of the gesture arena tests don't expect that to happen, SO
        /// c) the tests don't have logic to handle that case.
        ///
        /// To use: pass a lambda which invokes `make_contenders()` to
        /// `GestureArena::new_for_test()`.
        pub(super) struct ContenderFactoryOnce {
            contenders: Cell<Option<Vec<StubContender>>>,
        }

        impl ContenderFactoryOnce {
            pub(super) fn new(contenders: Vec<StubContender>) -> Self {
                Self { contenders: Cell::new(Some(contenders)) }
            }

            pub(super) fn make_contenders(&self) -> Vec<Box<dyn Contender>> {
                self.contenders
                    .take()
                    .expect("`contenders` has been consumed")
                    .into_iter()
                    .map(std::convert::From::<StubContender>::from)
                    .collect()
            }
        }

        impl Contender for StubContender {
            fn examine_event(self: Box<Self>, _event: &TouchpadEvent) -> ExamineEventResult {
                let mut inner = self.inner.borrow_mut();
                inner.calls_received += 1;
                inner.next_result.take().unwrap_or_else(|| {
                    panic!("missing `next_result` on call {}", inner.calls_received)
                })
            }
        }

        // A fake contender that always returns itself. Useful for keeping the
        // gesture arena from exiting the matching state. Keeping the arena in
        // the matching state eliminates the need for some tests to provide
        // return values for `process_buffered_events()`.
        #[derive(Debug)]
        pub(super) struct ContenderForever {}

        impl Contender for ContenderForever {
            fn examine_event(self: Box<Self>, _event: &TouchpadEvent) -> ExamineEventResult {
                ExamineEventResult::Contender(self)
            }
        }

        #[derive(Clone, Debug)]
        pub(super) struct StubMatchedContender {
            inner: Rc<RefCell<StubMatchedContenderInner>>,
        }

        impl StubMatchedContender {
            pub(super) fn new() -> Self {
                Self {
                    inner: Rc::new(RefCell::new(StubMatchedContenderInner {
                        next_verify_event_result: None,
                        next_process_buffered_events_result: None,
                        verify_event_calls_received: 0,
                        process_buffered_events_calls_received: 0,
                        last_process_buffered_events_args: None,
                    })),
                }
            }

            /// Set the value to be returned on the next call to `verify_event()`.
            /// Aborts if a value is already set, since that suggests that a previously
            /// expected call was never made.
            pub(super) fn set_next_verify_event_result(&self, next_result: VerifyEventResult) {
                self.assert_next_verify_event_result_is_none();
                self.inner.borrow_mut().next_verify_event_result = Some(next_result);
            }

            fn assert_next_verify_event_result_is_none(&self) {
                assert_matches!(self.inner.borrow().next_verify_event_result, None);
            }

            pub(super) fn verify_event_calls_received(&self) -> usize {
                self.inner.borrow().verify_event_calls_received
            }

            /// Set the value to be returned on the next call to `process_buffered_events()`.
            /// Aborts if a value is already set, since that suggests that a previously
            /// expected call was never made.
            pub(super) fn set_next_process_buffered_events_result(
                &self,
                next_result: ProcessBufferedEventsResult,
            ) {
                self.assert_next_process_buffered_events_result_is_none();
                self.inner.borrow_mut().next_process_buffered_events_result = Some(next_result);
            }

            pub(super) fn get_last_processed_buffered_events_args(
                &self,
            ) -> Option<Vec<TouchpadEvent>> {
                self.inner.borrow_mut().last_process_buffered_events_args.take()
            }

            fn assert_next_process_buffered_events_result_is_none(&self) {
                assert_matches!(self.inner.borrow().next_process_buffered_events_result, None);
            }

            pub(super) fn ref_count(&self) -> usize {
                Rc::strong_count(&self.inner)
            }
        }

        #[derive(Debug)]
        struct StubMatchedContenderInner {
            next_verify_event_result: Option<VerifyEventResult>,
            next_process_buffered_events_result: Option<ProcessBufferedEventsResult>,
            verify_event_calls_received: usize,
            process_buffered_events_calls_received: usize,
            last_process_buffered_events_args: Option<Vec<TouchpadEvent>>,
        }

        impl MatchedContender for StubMatchedContender {
            fn verify_event(self: Box<Self>, _event: &TouchpadEvent) -> VerifyEventResult {
                let mut inner = self.inner.borrow_mut();
                inner.verify_event_calls_received += 1;
                inner.next_verify_event_result.take().unwrap_or_else(|| {
                    panic!(
                        "missing `next_verify_event_result` on call {}",
                        inner.verify_event_calls_received
                    )
                })
            }

            fn process_buffered_events(
                self: Box<Self>,
                events: Vec<TouchpadEvent>,
            ) -> ProcessBufferedEventsResult {
                let mut inner = self.inner.borrow_mut();
                inner.last_process_buffered_events_args = Some(events);
                inner.process_buffered_events_calls_received += 1;
                inner.next_process_buffered_events_result.take().unwrap_or_else(|| {
                    panic!(
                        "missing `next_process_buffered_events_result` on call {}",
                        inner.process_buffered_events_calls_received
                    )
                })
            }
        }

        #[derive(Clone, Debug)]
        pub(super) struct StubWinner {
            inner: Rc<RefCell<StubWinnerInner>>,
        }

        impl StubWinner {
            pub(super) fn new() -> Self {
                Self {
                    inner: Rc::new(RefCell::new(StubWinnerInner {
                        next_result: None,
                        calls_received: 0,
                    })),
                }
            }

            /// Set the value to be returned on the next call to `examine_event()`.
            pub(super) fn set_next_result(&self, next_result: ProcessNewEventResult) {
                self.inner.borrow_mut().next_result = Some(next_result);
            }

            pub(super) fn calls_received(&self) -> usize {
                self.inner.borrow().calls_received
            }
        }

        #[derive(Debug)]
        struct StubWinnerInner {
            next_result: Option<ProcessNewEventResult>,
            calls_received: usize,
        }

        impl Winner for StubWinner {
            fn process_new_event(self: Box<Self>, _event: TouchpadEvent) -> ProcessNewEventResult {
                let mut inner = self.inner.borrow_mut();
                inner.calls_received += 1;
                inner.next_result.take().unwrap_or_else(|| {
                    panic!("missing `next_result` on call {}", inner.calls_received)
                })
            }
        }

        impl From<StubContender> for Box<dyn Contender> {
            fn from(stub_contender: StubContender) -> Box<dyn Contender> {
                Box::new(stub_contender)
            }
        }

        impl From<ContenderForever> for Box<dyn Contender> {
            fn from(contender_forever: ContenderForever) -> Box<dyn Contender> {
                Box::new(contender_forever)
            }
        }

        impl From<StubMatchedContender> for Box<dyn MatchedContender> {
            fn from(stub_matched_contender: StubMatchedContender) -> Box<dyn MatchedContender> {
                Box::new(stub_matched_contender)
            }
        }

        impl From<StubWinner> for Box<dyn Winner> {
            fn from(stub_winner: StubWinner) -> Box<dyn Winner> {
                Box::new(stub_winner)
            }
        }
    }

    mod idle_state {
        use {
            super::{
                super::{
                    ExamineEventResult, MutableState, TouchpadEvent, UnhandledInputHandler,
                    _GestureArena,
                },
                utils::{
                    make_touchpad_descriptor, make_unhandled_mouse_event,
                    make_unhandled_touchpad_event, ContenderFactoryOnce, StubContender,
                    StubMatchedContender,
                },
            },
            crate::{input_device, touch_binding},
            assert_matches::assert_matches,
            fuchsia_zircon as zx,
            maplit::hashset,
            pretty_assertions::assert_eq,
            std::{cell::Cell, rc::Rc},
        };

        #[fuchsia::test(allow_stalls = false)]
        async fn invokes_contender_factory_on_touchpad_event() {
            let contender_factory_called = Cell::new(false);
            let contender_factory = || {
                contender_factory_called.set(true);
                Vec::new()
            };
            let arena = Rc::new(_GestureArena::new_for_test(contender_factory));
            arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert!(contender_factory_called.get());
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn does_not_invoke_contender_factory_on_mouse_event() {
            let contender_factory_called = Cell::new(false);
            let contender_factory = || {
                contender_factory_called.set(true);
                Vec::new()
            };
            let arena = Rc::new(_GestureArena::new_for_test(contender_factory));
            arena.handle_unhandled_input_event(make_unhandled_mouse_event()).await;
            assert!(!contender_factory_called.get());
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn calls_examine_event_on_contender() {
            let contender = StubContender::new();
            let contender_factory = ContenderFactoryOnce::new(vec![contender.clone()]);
            let arena =
                Rc::new(_GestureArena::new_for_test(|| contender_factory.make_contenders()));
            contender.set_next_result(ExamineEventResult::Mismatch);
            arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_eq!(contender.calls_received(), 1);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn calls_examine_event_on_all_contenders_even_if_first_matches() {
            let first_contender = StubContender::new();
            let second_contender = StubContender::new();
            let contender_factory =
                ContenderFactoryOnce::new(vec![first_contender.clone(), second_contender.clone()]);
            let arena =
                Rc::new(_GestureArena::new_for_test(|| contender_factory.make_contenders()));
            first_contender.set_next_result(ExamineEventResult::MatchedContender(
                StubMatchedContender::new().into(),
            ));
            second_contender.set_next_result(ExamineEventResult::Mismatch);
            arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_eq!(first_contender.calls_received(), 1);
            assert_eq!(second_contender.calls_received(), 1);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn retains_reference_to_replacement_contender() {
            // Create a gesture arena which will instantiate a `StubContender`.
            let initial_contender = StubContender::new();
            let contender_factory = ContenderFactoryOnce::new(vec![initial_contender.clone()]);
            let arena =
                Rc::new(_GestureArena::new_for_test(|| contender_factory.make_contenders()));

            // Configure `initial_contender` to return a new `StubContender` when
            // `examine_event()` is called.
            let replacement_contender = StubContender::new();
            initial_contender.set_next_result(ExamineEventResult::Contender(
                replacement_contender.clone().into(),
            ));

            // Process a touchpad event. This should cause `arena` to consume the
            // `ExamineEventResult` set above.
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;

            // Verify that the `ExamineEventResult` was, in fact, consumed.
            initial_contender.assert_next_result_is_none();

            // Finally, verify that `replacement_contender` has two references.
            //
            // Note that:
            // * One of the references is from `replacement_contender`.
            // * The second one cannot be from the `ExamineEventResult` above,
            //   because the `ExamineEventResult` was consumed.
            //
            // Hence: the second reference (if it exists) must be in the gesture arena.
            assert_eq!(replacement_contender.ref_count(), 2);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn retains_reference_to_matched_contender() {
            // Create a gesture arena which will instantiate a `StubContender`.
            let initial_contender = StubContender::new();
            let contender_factory = ContenderFactoryOnce::new(vec![initial_contender.clone()]);
            let arena =
                Rc::new(_GestureArena::new_for_test(|| contender_factory.make_contenders()));

            // Configure `initial_contender` to return a `StubMatchedContender` when
            // `examine_event()` is called.
            let replacement_contender = StubMatchedContender::new();
            initial_contender.set_next_result(ExamineEventResult::MatchedContender(
                replacement_contender.clone().into(),
            ));

            // Process a touchpad event. This should cause `arena` to consume the
            // `ExamineEventResult` set above.
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;

            // Verify that the `ExamineEventResult` was, in fact, consumed.
            initial_contender.assert_next_result_is_none();

            // Finally, verify that `replacement_contender` has two references.
            //
            // Note that:
            // * One of the references is from `replacement_contender`.
            // * The second one cannot be from the `ExamineEventResult` above,
            //   because the `ExamineEventResult` was consumed.
            //
            // Hence: the second reference (if it exists) must be in the gesture arena.
            assert_eq!(replacement_contender.ref_count(), 2);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn retains_touchpad_event_when_entering_matching() {
            // Create a gesture arena which will instantiate a `StubContender`.
            let initial_contender = StubContender::new();
            let contender_factory = ContenderFactoryOnce::new(vec![initial_contender.clone()]);
            let arena =
                Rc::new(_GestureArena::new_for_test(|| contender_factory.make_contenders()));

            // Create the event which will be sent to the arena.
            let touchpad_event = input_device::UnhandledInputEvent {
                event_time: zx::Time::from_nanos(123456),
                device_event: input_device::InputDeviceEvent::Touchpad(
                    touch_binding::TouchpadEvent {
                        injector_contacts: vec![],
                        pressed_buttons: hashset! {0},
                    },
                ),
                device_descriptor: make_touchpad_descriptor(),
                trace_id: None,
            };

            // Configure `initial_contender` to return a `StubMatchedContender` when
            // `examine_event()` is called.
            initial_contender.set_next_result(ExamineEventResult::MatchedContender(
                StubMatchedContender::new().into(),
            ));

            // Process `touchpad_event`. Because `initial_contender` returns
            // `ExamineEventResult::MatchedContender`, the gesture arena will enter
            // the `Matching` state.
            arena.clone().handle_unhandled_input_event(touchpad_event).await;

            // Verify that `arena` retained the details of `touchpad_event`.
            assert_matches!(
                &*arena.mutable_state.borrow(),
                MutableState::Matching {
                    contenders: _,
                    matched_contenders: _,
                    buffered_events
                } => assert_eq!(
                    buffered_events.as_slice(),
                    [TouchpadEvent {
                        timestamp: zx::Time::from_nanos(123456),
                        pressed_buttons: vec![0],
                        contacts: vec![],
                    }]
                )
            );
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn generates_no_events_on_mismatch() {
            let contender = StubContender::new();
            let contender_factory = ContenderFactoryOnce::new(vec![contender.clone()]);
            let arena =
                Rc::new(_GestureArena::new_for_test(|| contender_factory.make_contenders()));
            contender.set_next_result(ExamineEventResult::Mismatch);
            assert_eq!(
                arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await,
                vec![]
            );
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn generates_no_events_when_entering_matching() {
            let contender = StubContender::new();
            let contender_factory = ContenderFactoryOnce::new(vec![contender.clone()]);
            let arena =
                Rc::new(_GestureArena::new_for_test(|| contender_factory.make_contenders()));
            contender.set_next_result(ExamineEventResult::Contender(StubContender::new().into()));
            assert_eq!(
                arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await,
                vec![]
            );
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn enters_idle_on_mismatch() {
            let contender = StubContender::new();
            let contender_factory = ContenderFactoryOnce::new(vec![contender.clone()]);
            let arena =
                Rc::new(_GestureArena::new_for_test(|| contender_factory.make_contenders()));
            contender.set_next_result(ExamineEventResult::Mismatch);
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Idle);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn enters_matching_on_contender_result() {
            let contender = StubContender::new();
            let contender_factory = ContenderFactoryOnce::new(vec![contender.clone()]);
            let arena =
                Rc::new(_GestureArena::new_for_test(|| contender_factory.make_contenders()));
            contender.set_next_result(ExamineEventResult::Contender(StubContender::new().into()));
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Matching { .. });
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn enters_matching_on_matched_contender_result() {
            let contender = StubContender::new();
            let contender_factory = ContenderFactoryOnce::new(vec![contender.clone()]);
            let arena =
                Rc::new(_GestureArena::new_for_test(|| contender_factory.make_contenders()));
            contender.set_next_result(ExamineEventResult::MatchedContender(
                StubMatchedContender::new().into(),
            ));
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Matching { .. });
        }
    }

    mod matching_state {
        use {
            super::{
                super::{
                    Contender, ExamineEventResult, MouseEvent, MutableState,
                    ProcessBufferedEventsResult, RecognizedGesture, TouchpadEvent,
                    UnhandledInputHandler, VerifyEventResult, _GestureArena, PRIMARY_BUTTON,
                },
                utils::{
                    make_touchpad_descriptor, make_unhandled_mouse_event,
                    make_unhandled_touchpad_event, ContenderForever, StubContender,
                    StubMatchedContender, StubWinner,
                },
            },
            crate::{input_device, mouse_binding, touch_binding, Position},
            assert_matches::assert_matches,
            fuchsia_zircon as zx,
            maplit::hashset,
            pretty_assertions::assert_eq,
            std::{cell::RefCell, rc::Rc},
            test_case::test_case,
        };

        fn make_matching_arena(
            contenders: Vec<StubContender>,
            matched_contenders: Vec<StubMatchedContender>,
            buffered_events: Vec<TouchpadEvent>,
            contender_forever: Option<ContenderForever>,
        ) -> Rc<_GestureArena<impl Fn() -> Vec<Box<dyn Contender>>>> {
            Rc::new(_GestureArena {
                contender_factory: || {
                    // Note: printing instead of panic()-ing here yields better
                    // failure messages from the tests.
                    eprintln!("factory invoked in matching state");
                    vec![]
                },
                mutable_state: RefCell::new(MutableState::Matching {
                    contenders: {
                        contenders
                            .into_iter()
                            .map(std::convert::From::<StubContender>::from)
                            .chain(
                                contender_forever
                                    .into_iter()
                                    .map(std::convert::From::<ContenderForever>::from),
                            )
                            .collect()
                    },
                    matched_contenders: {
                        matched_contenders
                            .into_iter()
                            .map(std::convert::From::<StubMatchedContender>::from)
                            .collect()
                    },
                    buffered_events,
                }),
            })
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn invokes_examine_and_verify_event_on_touchpad_event() {
            let contender = StubContender::new();
            let matched_contender = StubMatchedContender::new();
            let arena = make_matching_arena(
                vec![contender.clone()],
                vec![matched_contender.clone()],
                vec![],
                None,
            );
            contender.set_next_result(ExamineEventResult::Contender(contender.clone().into()));
            matched_contender.set_next_verify_event_result(VerifyEventResult::Mismatch);
            arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_eq!(contender.calls_received(), 1);
            assert_eq!(matched_contender.verify_event_calls_received(), 1);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn does_not_invoke_examine_or_verify_event_on_mouse_event() {
            let contender = StubContender::new();
            let matched_contender = StubMatchedContender::new();
            let arena = make_matching_arena(
                vec![contender.clone()],
                vec![matched_contender.clone()],
                vec![],
                None,
            );
            contender.set_next_result(ExamineEventResult::Mismatch);
            matched_contender.set_next_verify_event_result(VerifyEventResult::Mismatch);
            arena.handle_unhandled_input_event(make_unhandled_mouse_event()).await;
            assert_eq!(contender.calls_received(), 0);
            assert_eq!(matched_contender.verify_event_calls_received(), 0);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn does_not_repeat_event_to_matched_contender_returned_by_examine_event() {
            let contender = StubContender::new();
            let arena = make_matching_arena(
                vec![contender.clone()],
                vec![],
                vec![],
                // Make sure that `arena` does not progress to the forwarding state,
                // even if the logic _does_ repeat the event.
                Some(ContenderForever {}),
            );

            // Configure `contender` to give a `MatchContender` to `arena`.
            let matched_contender = StubMatchedContender::new();
            contender.set_next_result(ExamineEventResult::MatchedContender(
                matched_contender.clone().into(),
            ));

            // Set the return value for `matched_contender`. If the implementation
            // is buggy, and `verify_event()` is called, having a return value for
            // that call makes this test fail at the final assertion, which is easier
            // to understand.
            matched_contender.set_next_verify_event_result(VerifyEventResult::Mismatch);

            // Send the touchpad event, and validate that the arena did not call
            // `verify_event()` on the newly returned `MatchedContender`.
            arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_eq!(matched_contender.verify_event_calls_received(), 0);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn invokes_examine_event_for_new_event_with_contender_replaced_by_contender() {
            // Set up an arena with a `StubContender` that will continue the
            // matching process with a new `StubContender`.
            let initial_contender = StubContender::new();
            let arena = make_matching_arena(vec![initial_contender.clone()], vec![], vec![], None);
            let replacement_contender = StubContender::new();
            initial_contender.set_next_result(ExamineEventResult::Contender(
                replacement_contender.clone().into(),
            ));

            // Process a touchpad event. This should cause `arena` to replace
            // `initial_contender` with `replacement_contender`.
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;

            // Process another touchpad event. This should cause `arena` to invoke
            // `examine_event()` on `replacement_contender`.
            replacement_contender.set_next_result(ExamineEventResult::Mismatch);
            arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await;

            // Verify that `replacement_contender` was called.
            assert_eq!(replacement_contender.calls_received(), 1);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn invokes_verify_event_for_new_event_with_contender_replaced_by_matched_contender() {
            // Set up an arena with
            // * a `StubContender` that will continue the matching process with
            //   a new `StubMatchedContender`
            // * a `ContenderForever`, which will keep the arena in the matching
            //   state
            let initial_contender = StubContender::new();
            let arena = make_matching_arena(
                vec![initial_contender.clone()],
                vec![],
                vec![],
                Some(ContenderForever {}),
            );
            let replacement_contender = StubMatchedContender::new();
            initial_contender.set_next_result(ExamineEventResult::MatchedContender(
                replacement_contender.clone().into(),
            ));

            // Process a touchpad event. This should cause `arena` to replace
            // `initial_contender` with `replacement_contender`.
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;

            // Process another touchpad event. This should cause `arena` to invoke
            // `examine_event()` on `replacement_contender`.
            replacement_contender.set_next_verify_event_result(VerifyEventResult::Mismatch);
            arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await;

            // Verify that `replacement_contender` was called.
            assert_eq!(replacement_contender.verify_event_calls_received(), 1);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn invokes_verify_event_for_new_event_with_matched_contender_replaced_by_matched_contender(
        ) {
            let matched_contender = StubMatchedContender::new();
            let arena = make_matching_arena(
                vec![],
                vec![matched_contender.clone()],
                vec![],
                // Ensure that `arena` remains in the matching state. This simplifies
                // the test by eliminating the need to provide a response to a
                // potential `process_buffered_events()` call.
                Some(ContenderForever {}),
            );

            // Configure `matched_contender` to replace itself with
            // `replacement_matched_contender`.
            let replacement_matched_contender = StubMatchedContender::new();
            matched_contender.set_next_verify_event_result(VerifyEventResult::MatchedContender(
                replacement_matched_contender.clone().into(),
            ));

            // Process a touchpad event. This should cause `arena` to retain
            // replace `contender` with `replacement_contender`.
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;

            // Set a return value for the expected call on `replacement_contender`.
            replacement_matched_contender.set_next_verify_event_result(VerifyEventResult::Mismatch);

            // Process another touchpad event, and verify that `replacement_contender`
            // is called.
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_eq!(replacement_matched_contender.verify_event_calls_received(), 1);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn generates_no_events_on_mismatch() {
            let contender = StubContender::new();
            let arena = make_matching_arena(vec![contender.clone()], vec![], vec![], None);
            contender.set_next_result(ExamineEventResult::Mismatch);
            assert_eq!(
                arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await,
                vec![]
            );
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn generates_no_events_on_contender() {
            let contender = StubContender::new();
            let arena = make_matching_arena(vec![contender.clone()], vec![], vec![], None);
            contender.set_next_result(ExamineEventResult::Contender(contender.clone().into()));
            assert_eq!(
                arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await,
                vec![]
            );
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn generates_no_events_on_multiple_matched_contenders() {
            let first_matched_contender = StubMatchedContender::new();
            let second_matched_contender = StubMatchedContender::new();
            let arena = make_matching_arena(
                vec![],
                vec![first_matched_contender.clone(), second_matched_contender.clone()],
                vec![],
                None,
            );
            first_matched_contender.set_next_verify_event_result(
                VerifyEventResult::MatchedContender(first_matched_contender.clone().into()),
            );
            second_matched_contender.set_next_verify_event_result(
                VerifyEventResult::MatchedContender(second_matched_contender.clone().into()),
            );
            assert_eq!(
                arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await,
                vec![]
            );
        }

        #[test_case(Some(StubWinner::new()); "with_winner")]
        #[test_case(None; "without_winner")]
        #[fuchsia::test(allow_stalls = false)]
        async fn generates_events_from_process_buffered_events_on_single_matched_contender(
            winner: Option<StubWinner>,
        ) {
            let matched_contender = StubMatchedContender::new();
            let arena = make_matching_arena(vec![], vec![matched_contender.clone()], vec![], None);
            matched_contender.set_next_verify_event_result(VerifyEventResult::MatchedContender(
                matched_contender.clone().into(),
            ));
            matched_contender.set_next_process_buffered_events_result(
                ProcessBufferedEventsResult {
                    generated_events: vec![
                        MouseEvent {
                            timestamp: zx::Time::from_nanos(123),
                            mouse_data: mouse_binding::MouseEvent {
                                location: mouse_binding::MouseLocation::Relative(
                                    mouse_binding::RelativeLocation {
                                        counts: Position::zero(),
                                        millimeters: Position::zero(),
                                    },
                                ),
                                wheel_delta_v: None,
                                wheel_delta_h: None,
                                phase: mouse_binding::MousePhase::Down,
                                affected_buttons: hashset! { PRIMARY_BUTTON },
                                pressed_buttons: hashset! { PRIMARY_BUTTON },
                            },
                        },
                        MouseEvent {
                            timestamp: zx::Time::from_nanos(456),
                            mouse_data: mouse_binding::MouseEvent {
                                location: mouse_binding::MouseLocation::Relative(
                                    mouse_binding::RelativeLocation {
                                        counts: Position::zero(),
                                        millimeters: Position::zero(),
                                    },
                                ),
                                wheel_delta_v: None,
                                wheel_delta_h: None,
                                phase: mouse_binding::MousePhase::Up,
                                affected_buttons: hashset! { PRIMARY_BUTTON },
                                pressed_buttons: hashset! {},
                            },
                        },
                    ],
                    winner: winner.map(std::convert::From::<StubWinner>::from),
                    recognized_gesture: RecognizedGesture::Click,
                },
            );
            assert_matches!(
                arena
                    .handle_unhandled_input_event(make_unhandled_touchpad_event())
                    .await
                    .as_slice(),
                [
                    input_device::InputEvent {
                        handled: input_device::Handled::No,
                        device_event: input_device::InputDeviceEvent::Mouse(
                            mouse_binding::MouseEvent {
                                pressed_buttons: first_pressed_buttons, ..
                            }
                        ),
                        ..
                    },
                    input_device::InputEvent {
                        handled: input_device::Handled::No,
                        device_event: input_device::InputDeviceEvent::Mouse(
                            mouse_binding::MouseEvent {
                                pressed_buttons: second_pressed_buttons, ..
                            }
                        ),
                        ..
                    },
                ] => {
                    pretty_assertions::assert_eq!(*first_pressed_buttons, hashset! { PRIMARY_BUTTON });
                    pretty_assertions::assert_eq!(*second_pressed_buttons, hashset! {});
                }
            );
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn passes_all_buffered_events_to_process_buffered_events() {
            // Create an arena, and seed it a buffered event (emulating what happens
            // as the arena transitions from Idle to Matching).
            let contender = StubContender::new();
            let matched_contender = StubMatchedContender::new();
            let arena = make_matching_arena(
                vec![contender.clone()],
                vec![matched_contender.clone()],
                vec![TouchpadEvent {
                    timestamp: zx::Time::from_nanos(123),
                    contacts: vec![],
                    pressed_buttons: vec![],
                }],
                None,
            );

            // Send another event to the arena, to exercise the case of an event
            // being buffered because the contest continued.
            contender.set_next_result(ExamineEventResult::Contender(contender.clone().into()));
            matched_contender.set_next_verify_event_result(VerifyEventResult::MatchedContender(
                matched_contender.clone().into(),
            ));
            arena
                .clone()
                .handle_unhandled_input_event(input_device::UnhandledInputEvent {
                    event_time: zx::Time::from_nanos(456),
                    device_event: input_device::InputDeviceEvent::Touchpad(
                        touch_binding::TouchpadEvent {
                            injector_contacts: vec![],
                            pressed_buttons: hashset! {},
                        },
                    ),
                    device_descriptor: make_touchpad_descriptor(),
                    trace_id: None,
                })
                .await;

            // Send the event that concludes the contest.
            contender.set_next_result(ExamineEventResult::Mismatch);
            matched_contender.set_next_verify_event_result(VerifyEventResult::MatchedContender(
                matched_contender.clone().into(),
            ));
            matched_contender.set_next_process_buffered_events_result(
                ProcessBufferedEventsResult {
                    generated_events: vec![],
                    winner: None,
                    recognized_gesture: RecognizedGesture::Palm,
                },
            );
            arena
                .handle_unhandled_input_event(input_device::UnhandledInputEvent {
                    event_time: zx::Time::from_nanos(789),
                    device_event: input_device::InputDeviceEvent::Touchpad(
                        touch_binding::TouchpadEvent {
                            injector_contacts: vec![],
                            pressed_buttons: hashset! {},
                        },
                    ),
                    device_descriptor: make_touchpad_descriptor(),
                    trace_id: None,
                })
                .await;

            // Verify that the contender received all three events.
            assert_eq!(
                matched_contender
                    .get_last_processed_buffered_events_args()
                    .map(|vec| vec
                        .into_iter()
                        .map(|event| event.timestamp.into_nanos())
                        .collect::<Vec<_>>())
                    .as_deref(),
                Some([123, 456, 789].as_slice())
            );
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn transitions_to_idle_when_sole_contender_does_not_match() {
            let contender = StubContender::new();
            let arena = make_matching_arena(vec![contender.clone()], vec![], vec![], None);
            contender.set_next_result(ExamineEventResult::Mismatch);
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Idle);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn transitions_to_idle_when_sole_matched_contender_does_not_match() {
            let matched_contender = StubMatchedContender::new();
            let arena = make_matching_arena(vec![], vec![matched_contender.clone()], vec![], None);
            matched_contender.set_next_verify_event_result(VerifyEventResult::Mismatch);
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Idle);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn remains_in_matching_when_a_contender_remains() {
            let contender = StubContender::new();
            let arena = make_matching_arena(vec![contender.clone()], vec![], vec![], None);
            contender.set_next_result(ExamineEventResult::Contender(contender.clone().into()));
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Matching { .. });
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn remains_in_matching_when_multiple_matched_contenders_remain() {
            let matched_contender_a = StubMatchedContender::new();
            let matched_contender_b = StubMatchedContender::new();
            let arena = make_matching_arena(
                vec![],
                vec![matched_contender_a.clone(), matched_contender_b.clone()],
                vec![],
                None,
            );
            matched_contender_a.set_next_verify_event_result(VerifyEventResult::MatchedContender(
                matched_contender_a.clone().into(),
            ));
            matched_contender_b.set_next_verify_event_result(VerifyEventResult::MatchedContender(
                matched_contender_b.clone().into(),
            ));
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Matching { .. });
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn transitions_to_idle_when_sole_matched_contender_returns_no_winner() {
            let matched_contender = StubMatchedContender::new();
            let arena = make_matching_arena(vec![], vec![matched_contender.clone()], vec![], None);
            matched_contender.set_next_verify_event_result(VerifyEventResult::MatchedContender(
                matched_contender.clone().into(),
            ));
            matched_contender.set_next_process_buffered_events_result(
                ProcessBufferedEventsResult {
                    generated_events: vec![],
                    winner: None,
                    recognized_gesture: RecognizedGesture::Palm,
                },
            );
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Idle);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn transitions_to_forwarding_when_sole_matched_contender_returns_a_winner() {
            let matched_contender = StubMatchedContender::new();
            let arena = make_matching_arena(vec![], vec![matched_contender.clone()], vec![], None);
            matched_contender.set_next_verify_event_result(VerifyEventResult::MatchedContender(
                matched_contender.clone().into(),
            ));
            matched_contender.set_next_process_buffered_events_result(
                ProcessBufferedEventsResult {
                    generated_events: vec![],
                    winner: Some(StubWinner::new().into()),
                    recognized_gesture: RecognizedGesture::Palm,
                },
            );
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Forwarding { .. });
        }
    }

    mod forwarding_state {
        use {
            super::{
                super::{
                    Contender, ExamineEventResult, MouseEvent, MutableState, ProcessNewEventResult,
                    TouchpadEvent, UnhandledInputHandler, _GestureArena,
                },
                utils::{
                    make_unhandled_mouse_event, make_unhandled_touchpad_event, ContenderForever,
                    StubContender, StubWinner,
                },
            },
            crate::{input_device, mouse_binding, Position},
            assert_matches::assert_matches,
            fuchsia_zircon as zx,
            maplit::hashset,
            pretty_assertions::assert_eq,
            std::{
                cell::{Cell, RefCell},
                rc::Rc,
            },
            test_case::test_case,
        };

        /// Creates an arena in the forwarding state, with
        /// a) the given `winner`, and
        /// b) an appropriate contender factory
        ///
        /// If `contender` is `None`, the contender factory will abort on the first
        /// call.
        ///
        /// If `contender` is `Some`, the contender factory will return the content
        /// of the `Option` on the first call, and abort on the second call.
        ///
        /// The former is the common case for the tests. The latter is useful for
        /// tests that exercise the arena's handling of an `EndGesture(Some)`.
        fn make_forwarding_arena(
            winner: StubWinner,
            contender: Option<Box<dyn Contender>>,
        ) -> Rc<_GestureArena<impl Fn() -> Vec<Box<dyn Contender>>>> {
            let contender = Cell::new(contender);
            Rc::new(_GestureArena {
                contender_factory: move || vec![contender.take().expect("`contender` is None")],
                mutable_state: RefCell::new(MutableState::Forwarding { winner: winner.into() }),
            })
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn invokes_process_new_event_on_touchpad_event() {
            let winner = StubWinner::new();
            let arena = make_forwarding_arena(winner.clone(), None);
            winner.set_next_result(ProcessNewEventResult::EndGesture(None));
            arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_eq!(winner.calls_received(), 1);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn does_not_invoke_process_new_event_on_mouse_event() {
            let winner = StubWinner::new();
            let arena = make_forwarding_arena(winner.clone(), None);
            winner.set_next_result(ProcessNewEventResult::EndGesture(None));
            arena.handle_unhandled_input_event(make_unhandled_mouse_event()).await;
            assert_eq!(winner.calls_received(), 0);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn invokes_process_new_event_for_multiple_new_events() {
            // Create `arena` with `winner` and a `ContenderForever`. The latter
            // makes the test fail in a more useful way if the `GestureArena`
            // is buggy.
            let winner = StubWinner::new();
            let arena = make_forwarding_arena(winner.clone(), Some(ContenderForever {}.into()));

            // Send two events to `arena`.
            winner.set_next_result(ProcessNewEventResult::ContinueGesture(
                None,
                winner.clone().into(),
            ));
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            winner.set_next_result(ProcessNewEventResult::ContinueGesture(
                None,
                winner.clone().into(),
            ));
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;

            // Verify `winner` was called as expected.
            assert_eq!(winner.calls_received(), 2);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn generates_event_on_continue_gesture_with_mouse_event() {
            let winner = StubWinner::new();
            let arena = make_forwarding_arena(winner.clone(), None);
            winner.set_next_result(ProcessNewEventResult::ContinueGesture(
                Some(MouseEvent {
                    timestamp: zx::Time::from_nanos(123),
                    mouse_data: mouse_binding::MouseEvent {
                        location: mouse_binding::MouseLocation::Relative(
                            mouse_binding::RelativeLocation {
                                counts: Position::zero(),
                                millimeters: Position::zero(),
                            },
                        ),
                        wheel_delta_v: None,
                        wheel_delta_h: None,
                        phase: mouse_binding::MousePhase::Move,
                        affected_buttons: hashset! {},
                        pressed_buttons: hashset! {},
                    },
                }),
                winner.clone().into(),
            ));
            assert_matches!(
                arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await.as_slice(),
                [
                    input_device::InputEvent {
                        event_time,
                        handled: input_device::Handled::No,
                        device_event: input_device::InputDeviceEvent::Mouse(_),
                        ..
                    },
                ] => pretty_assertions::assert_eq!(*event_time, zx::Time::from_nanos(123))
            );
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn generates_no_events_on_continue_gesture_without_mouse_event() {
            let winner = StubWinner::new();
            let arena = make_forwarding_arena(winner.clone(), None);
            winner.set_next_result(ProcessNewEventResult::ContinueGesture(
                None,
                winner.clone().into(),
            ));
            pretty_assertions::assert_eq!(
                arena
                    .handle_unhandled_input_event(make_unhandled_touchpad_event())
                    .await
                    .as_slice(),
                vec![]
            );
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn generates_no_events_on_end_gesture_without_touchpad_event() {
            let winner = StubWinner::new();
            let arena = make_forwarding_arena(winner.clone(), None);
            winner.set_next_result(ProcessNewEventResult::EndGesture(None));
            pretty_assertions::assert_eq!(
                arena
                    .handle_unhandled_input_event(make_unhandled_touchpad_event())
                    .await
                    .as_slice(),
                vec![]
            );
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn generates_no_events_on_end_gesture_with_touchpad_event() {
            // Create `arena` with a `StubContender` for processing the unconsumed
            // `TouchpadEvent`.
            let winner = StubWinner::new();
            let contender = StubContender::new();
            let arena = make_forwarding_arena(winner.clone(), Some(contender.clone().into()));
            winner.set_next_result(ProcessNewEventResult::EndGesture(Some(TouchpadEvent {
                contacts: vec![],
                pressed_buttons: vec![],
                timestamp: zx::Time::ZERO,
            })));

            // Set a return value for the `examine_event()` call.
            contender.set_next_result(ExamineEventResult::Mismatch);

            // Verify no events were generated.
            pretty_assertions::assert_eq!(
                arena
                    .handle_unhandled_input_event(make_unhandled_touchpad_event())
                    .await
                    .as_slice(),
                vec![]
            );
        }

        #[test_case(Some(MouseEvent{
            timestamp: zx::Time::from_nanos(123),
            mouse_data: mouse_binding::MouseEvent {
                location: mouse_binding::MouseLocation::Relative(
                    mouse_binding::RelativeLocation {
                        counts: Position::zero(),
                        millimeters: Position::zero(),
                    },
                ),
                wheel_delta_v: None,
                wheel_delta_h: None,
                phase: mouse_binding::MousePhase::Move,
                affected_buttons: hashset! {},
                pressed_buttons: hashset! {},
            },
        }); "with_mouse_event")]
        #[test_case(None; "without_mouse_event")]
        #[fuchsia::test(allow_stalls = false)]
        async fn remains_in_forwarding_on_continue_gesture(mouse_event: Option<MouseEvent>) {
            let winner = StubWinner::new();
            let arena = make_forwarding_arena(winner.clone(), None);
            winner.set_next_result(ProcessNewEventResult::ContinueGesture(
                mouse_event,
                winner.clone().into(),
            ));
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Forwarding { .. });
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn transitions_to_idle_on_end_gesture_without_touchpad_event() {
            let winner = StubWinner::new();
            let arena = make_forwarding_arena(winner.clone(), None);
            winner.set_next_result(ProcessNewEventResult::EndGesture(None));
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Idle);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn starts_new_contest_on_end_gesture_with_touchpad_event() {
            // Set up an arena in the forwarding state with `winner`, and
            // with a factory that will return `contender`. The latter should
            // be called at the start of the new contest.
            let winner = StubWinner::new();
            let contender = StubContender::new();
            let arena = make_forwarding_arena(winner.clone(), Some(contender.clone().into()));

            // Set up `winner` to end the gesture and return an unconsumed event.
            winner.set_next_result(ProcessNewEventResult::EndGesture(Some(TouchpadEvent {
                timestamp: zx::Time::ZERO,
                contacts: vec![],
                pressed_buttons: vec![],
            })));

            // Set up `contender` to reply to the `examine_event()` call.
            contender.set_next_result(ExamineEventResult::Mismatch);

            // Send an event into the arena.
            arena.handle_unhandled_input_event(make_unhandled_touchpad_event()).await;

            // Verify that the arena started a new contest.
            assert_eq!(contender.calls_received(), 1);
        }
    }
}
