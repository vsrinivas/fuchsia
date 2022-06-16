// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{input_device, input_handler::UnhandledInputHandler, mouse_binding::MouseEvent},
    async_trait::async_trait,
};

// TODO(https://fxbug.dev/102655): replace this with an import of
// `touch_binding::TouchpadEvent`.
struct _TouchpadEvent {}

// TODO(https://fxbug.dev/102654): check that we've removed all leading `_` from types
// and variables in this file.

enum _ExamineEventResult {
    Contender(Box<dyn Contender>),
    MatchedContender(Box<dyn MatchedContender>),
    Mismatch,
}

trait Contender {
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
    fn examine_event(self: Box<Self>, event: &_TouchpadEvent) -> _ExamineEventResult;
}

enum _VerifyEventResult {
    MatchedContender(Box<dyn MatchedContender>),
    Mismatch,
}

enum _RecognizedGesture {
    /// Contains one variant for each recognizer, and the
    /// special value `Unrecognized` for when no recognizer
    /// claims the gesture.
    Unrecognized,
}

struct _ProcessBufferedEventsResult {
    generated_events: Vec<MouseEvent>,
    winner: Option<Box<dyn Winner>>,
    recognized_gesture: _RecognizedGesture, // for latency breakdown
}

trait MatchedContender {
    /// Verifies that `event` still matches the gesture that is relevant
    /// to this `Recognizer`.
    ///
    /// Returns
    /// * `VerifyEventResult::MatchedContender` if this recognizer wants
    ///   to send (or start sending) events downstream, OR
    /// * `VerifyEventResult::Mismatch` if this recognizer no longer
    ///   wants to contend for this gesture
    fn verify_event(self: Box<Self>, event: &_TouchpadEvent) -> _VerifyEventResult;

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
        events: Vec<_TouchpadEvent>,
    ) -> _ProcessBufferedEventsResult;
}

enum _ProcessNewEventResult {
    ContinueGesture(Option<MouseEvent>, Box<dyn Winner>),
    EndGesture(Option<_TouchpadEvent>),
}

trait Winner {
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
    fn process_new_event(self: Box<Self>, event: _TouchpadEvent) -> _ProcessNewEventResult;
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
