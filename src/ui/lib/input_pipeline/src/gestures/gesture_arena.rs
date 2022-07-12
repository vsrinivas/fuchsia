// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{input_device, input_handler::UnhandledInputHandler, mouse_binding, touch_binding},
    async_trait::async_trait,
    fuchsia_zircon as zx,
    std::any::Any,
    std::fmt::Debug,
};

// TODO(https://fxbug.dev/102654): check that we've removed all leading `_` from types
// and variables in this file.
#[derive(Debug, Clone)]
pub(super) struct TouchpadEvent {
    pub(super) timestamp: zx::Time,
    // TODO(https://fxbug.dev/102655): replace these fields with a field that embeds
    // `touch_data: super::touch_binding::TouchpadEvent`.
    pub(super) pressed_buttons: Vec<u8>,
    pub(super) contacts: Vec<touch_binding::TouchContact>,
}

#[derive(Debug, PartialEq)]
pub(super) struct MouseEvent {
    pub(super) _timestamp: zx::Time,
    pub(super) _mouse_data: mouse_binding::MouseEvent,
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

pub(super) enum _ProcessNewEventResult {
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
    fn process_new_event(self: Box<Self>, event: TouchpadEvent) -> _ProcessNewEventResult;
}

struct _GestureArena {}

#[async_trait(?Send)]
impl UnhandledInputHandler for _GestureArena {
    /// Interprets `TouchpadEvent`s, and sends corresponding
    /// `MouseEvent`s downstream.
    async fn handle_unhandled_input_event(
        self: std::rc::Rc<Self>,
        _unhandled_input_event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent> {
        // TODO(https://fxbug.dev/102656) implement this method.
        unimplemented!();
    }
}
