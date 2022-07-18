// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{input_device, input_handler::UnhandledInputHandler, mouse_binding, touch_binding},
    async_trait::async_trait,
    core::cell::RefCell,
    fuchsia_syslog::fx_log_debug,
    fuchsia_zircon as zx,
    std::any::Any,
    std::fmt::Debug,
};

pub(super) const PRIMARY_BUTTON: mouse_binding::MouseButton = 1;

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
    fn from(_mouse_event: MouseEvent) -> input_device::InputEvent {
        todo!()
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
            //   into something clients downstrea of the input pipeline can
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
        _contenders: Vec<Box<dyn Contender>>,
        _matched_contenders: Vec<Box<dyn MatchedContender>>,
        _buffered_events: Vec<TouchpadEvent>,
        _new_event: TouchpadEvent,
    ) -> (MutableState, Vec<MouseEvent>) {
        todo!()
    }

    fn handle_event_while_forwarding(
        &self,
        _winner: Box<dyn Winner>,
        _new_event: TouchpadEvent,
    ) -> (MutableState, Vec<MouseEvent>) {
        todo!()
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
                TouchpadEvent, VerifyEventResult, PRIMARY_BUTTON,
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

        fn make_mouse_descriptor() -> input_device::InputDeviceDescriptor {
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
        pub(super) struct StubContenderInner {
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
                    .map(|c| {
                        let dyn_c: Box<dyn Contender> = Box::new(c);
                        dyn_c
                    })
                    .collect()
            }
        }

        impl Contender for StubContender {
            fn examine_event(self: Box<Self>, _event: &TouchpadEvent) -> ExamineEventResult {
                self.inner.borrow_mut().calls_received += 1;
                self.inner.borrow_mut().next_result.take().unwrap_or_else(|| {
                    panic!("missing `next_result` on call {}", self.inner.borrow().calls_received)
                })
            }
        }

        #[derive(Clone, Debug)]
        pub(super) struct StubMatchedContender {
            pub(super) inner: Rc<RefCell<StubMatchedContenderInner>>,
        }

        #[derive(Debug)]
        pub(super) struct StubMatchedContenderInner {}

        impl StubMatchedContender {
            pub(super) fn new() -> Self {
                Self { inner: Rc::new(RefCell::new(StubMatchedContenderInner {})) }
            }

            pub(super) fn ref_count(&self) -> usize {
                Rc::strong_count(&self.inner)
            }
        }

        impl MatchedContender for StubMatchedContender {
            fn verify_event(self: Box<Self>, _event: &TouchpadEvent) -> VerifyEventResult {
                todo!()
            }

            fn process_buffered_events(
                self: Box<Self>,
                _events: Vec<TouchpadEvent>,
            ) -> ProcessBufferedEventsResult {
                todo!()
            }
        }
    }

    mod idle_state {
        use {
            super::{
                super::{
                    Contender, ExamineEventResult, MatchedContender, MutableState, TouchpadEvent,
                    UnhandledInputHandler, _GestureArena,
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
            let matched_contender: Box<dyn MatchedContender> =
                Box::new(StubMatchedContender::new());
            first_contender
                .set_next_result(ExamineEventResult::MatchedContender(matched_contender));
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
            let dyn_replacement_contender: Box<dyn Contender> =
                Box::new(replacement_contender.clone());
            initial_contender
                .set_next_result(ExamineEventResult::Contender(dyn_replacement_contender));

            // Process a touchpad event. This should cause `arena` to consume the
            // `ExamineEventResult` set above.
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;

            // Verify that the `ExamineEventResult` was, in fact, consumed.
            initial_contender.assert_next_result_is_none();

            // Finally, verify that the `StubContenderInner` referenced by `replacement_contender`
            // has two references.
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
            let dyn_replacement_contender: Box<dyn MatchedContender> =
                Box::new(replacement_contender.clone());
            initial_contender
                .set_next_result(ExamineEventResult::MatchedContender(dyn_replacement_contender));

            // Process a touchpad event. This should cause `arena` to consume the
            // `ExamineEventResult` set above.
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;

            // Verify that the `ExamineEventResult` was, in fact, consumed.
            initial_contender.assert_next_result_is_none();

            // Finally, verify that the `StubMatchedContenderInner` referenced by
            // `replacement_contender` has two references.
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
            let replacement_contender: Box<dyn MatchedContender> =
                Box::new(StubMatchedContender::new());
            initial_contender
                .set_next_result(ExamineEventResult::MatchedContender(replacement_contender));

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
            let replacement_contender: Box<dyn Contender> = Box::new(StubContender::new());
            contender.set_next_result(ExamineEventResult::Contender(replacement_contender));
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
            let replacement_contender: Box<dyn Contender> = Box::new(StubContender::new());
            contender.set_next_result(ExamineEventResult::Contender(replacement_contender));
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Matching { .. });
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn enters_matching_on_matched_contender_result() {
            let contender = StubContender::new();
            let contender_factory = ContenderFactoryOnce::new(vec![contender.clone()]);
            let arena =
                Rc::new(_GestureArena::new_for_test(|| contender_factory.make_contenders()));
            let replacement_contender: Box<dyn MatchedContender> =
                Box::new(StubMatchedContender::new());
            contender.set_next_result(ExamineEventResult::MatchedContender(replacement_contender));
            arena.clone().handle_unhandled_input_event(make_unhandled_touchpad_event()).await;
            assert_matches!(*arena.mutable_state.borrow(), MutableState::Matching { .. });
        }
    }
}
