// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/89234): Check what happens with the modifier keys - we should perhaps maintain
// them.

//! Implements hardware key autorepeat.
//!
//! The [Autorepeater] is a bit of an exception among the stages of the input pipeline.  This
//! handler does not implement [input_pipeline::InputHandler], as it requires a different approach
//! to event processing.
//!
//! Specifically, it requires the ability to interleave the events it generates into the
//! flow of "regular" events through the input pipeline.  While the [input_pipeline::InputHandler]
//! trait could in principle be modified to admit this sort of approach to event processing, in
//! practice the [Autorepeater] is for now the only stage that requires this approach, so it is
//! not cost effective to retrofit all other handlers just for the sake of this one.  We may
//! revisit this decision if we grow more stages that need autorepeat.

use crate::input_device::{self, Handled, InputDeviceDescriptor, InputDeviceEvent, InputEvent};
use crate::keyboard_binding::KeyboardEvent;
use anyhow::{anyhow, Context, Result};
use fidl_fuchsia_settings as fsettings;
use fidl_fuchsia_ui_input3::{KeyEventType, KeyMeaning};
use fuchsia_async::{Task, Time, Timer};
use fuchsia_syslog::{fx_log_debug, fx_log_err, fx_log_info, fx_log_warn};
use fuchsia_zircon as zx;
use fuchsia_zircon::Duration;
use futures::channel::mpsc::{self, UnboundedReceiver, UnboundedSender};
use futures::StreamExt;
use std::cell::RefCell;
use std::convert::TryInto;
use std::rc::Rc;

/// Typed autorepeat settings.  Use [Default::default()] and the `into_with_*`
/// to create a new instance.
#[derive(Debug, PartialEq, Clone, Copy)]
pub struct Settings {
    // The time delay before autorepeat kicks in.
    delay: Duration,
    // The average period between two successive autorepeats.  A reciprocal of
    // the autorepeat rate.
    period: Duration,
}

impl Default for Settings {
    fn default() -> Self {
        Settings { delay: Duration::from_millis(250), period: Duration::from_millis(50) }
    }
}

impl From<fsettings::Autorepeat> for Settings {
    /// Conversion, since [fsettings::Autorepeat] has untyped delay and period.
    fn from(s: fsettings::Autorepeat) -> Self {
        Self { delay: Duration::from_nanos(s.delay), period: Duration::from_nanos(s.period) }
    }
}

impl Settings {
    /// Modifies the delay.
    pub fn into_with_delay(self, delay: Duration) -> Self {
        Self { delay, ..self }
    }

    /// Modifies the period.
    pub fn into_with_period(self, period: Duration) -> Self {
        Self { period, ..self }
    }
}

// Whether the key is repeatable.
enum Repeatability {
    // The key may autorepeat.
    Yes,
    // The key may not autorepeat.
    No,
}

// Determines whether the given key meaning corresponds to a key that should be repeated.
//
// Roughly, the criterion is: if the key may contribute to text editing, it should be repeatable.
// The decisions taken below are a bit pragmatic, since our specification in the FIDL API is not
// exactly clear on when a key is repeatable.  Instead our rules are implicit in the way the key
// meaning is defined.
//
// To wit, nonzero code points are printable and therefore may repeat.
// NonPrintableKey are, paradoxically, repeatable, since the current examples of keys that are
// nonprintable are empirically repeatable.  Keys that are not repeatable have a zero as the
// codepoint - this is not an API requirement, but stems from the way we currently use it, so we
// may as well make it official.  The decisions may become obsolete as KeyMeaning evolves.
//
// TODO(fxbug.dev/89736): Sort out whether Left, Right, Up, Down etc should be nonprintable keys
// or keys with codepoint zero.
fn repeatability(key_meaning: Option<KeyMeaning>) -> Repeatability {
    match key_meaning {
        Some(KeyMeaning::Codepoint(0)) => Repeatability::No, // e.g. Shift
        Some(KeyMeaning::Codepoint(_)) => Repeatability::Yes, // A code point.
        // This will need to be extended when more nonprintable keys are introduced.
        Some(KeyMeaning::NonPrintableKey(_)) => Repeatability::Yes, // Tab, Enter, Backspace...
        None => Repeatability::Yes, // Printable with US QWERTY keymap.
    }
}

// An events multiplexer.
#[derive(Debug, Clone)]
enum AnyEvent {
    // A keyboard input event.
    Keyboard(KeyboardEvent, InputDeviceDescriptor, zx::Time, Handled),
    // An input event other than keyboard.
    NonKeyboard(InputEvent),
    // A timer event.
    Timeout,
}

impl TryInto<InputEvent> for AnyEvent {
    type Error = anyhow::Error;

    fn try_into(self) -> Result<InputEvent> {
        match self {
            AnyEvent::NonKeyboard(ev) => Ok(ev),
            AnyEvent::Keyboard(ev, device_descriptor, event_time, handled) => Ok(InputEvent {
                device_event: InputDeviceEvent::Keyboard(ev.clone()),
                device_descriptor: device_descriptor.clone(),
                event_time,
                handled,
            }),
            _ => Err(anyhow!("not an InputEvent: {:?}", &self)),
        }
    }
}

// The state of the autorepeat generator.
#[derive(Debug, Clone)]
enum State {
    /// Autorepeat is not active.
    Dormant,
    /// Autorepeat is armed, we are waiting for a timer event to expire, and when
    /// it does, we generate a repeat event.
    Armed {
        // The keyboard event that caused the state to become Armed.
        armed_event: KeyboardEvent,
        // The descriptor is used to reconstruct an InputEvent when needed.
        armed_descriptor: InputDeviceDescriptor,
        // The autorepeat timer is used in the various stages of autorepeat
        // waits.
        // Not used directly, but rather kept because of its side effect.
        _delay_timer: Rc<Task<()>>,
    },
}

impl Default for State {
    fn default() -> Self {
        State::Dormant
    }
}

// Logs an `event` before sending to `sink` for debugging.
fn unbounded_send_logged<T>(sink: &UnboundedSender<T>, event: T) -> Result<()>
where
    for<'a> &'a T: std::fmt::Debug,
    T: 'static + Sync + Send,
{
    fx_log_debug!("unbounded_send_logged: {:?}", &event);
    sink.unbounded_send(event)?;
    Ok(())
}

/// Creates a new autorepeat timer task.
///
/// The task will wait for the amount of time given in `delay`, and then send
/// a [AnyEvent::Timeout] to `sink`; unless it is canceled.
fn new_autorepeat_timer(sink: UnboundedSender<AnyEvent>, delay: Duration) -> Rc<Task<()>> {
    let task = Task::local(async move {
        Timer::new(Time::after(delay)).await;
        fx_log_debug!("autorepeat timeout");
        unbounded_send_logged(&sink, AnyEvent::Timeout)
            .unwrap_or_else(|e| fx_log_err!("could not fire autorepeat timer: {:?}", e));
    });
    Rc::new(task)
}

/// Maintains the internal autorepeat state.
///
/// The autorepeat tracks key presses and generates autorepeat key events for
/// the keys that are eligible for autorepeat.
pub struct Autorepeater {
    // Internal events are multiplexed into this sender.  We may make multiple
    // clones to serialize async events.
    event_sink: UnboundedSender<AnyEvent>,

    // This end is consumed to get the multiplexed ordered events.
    event_source: RefCell<UnboundedReceiver<AnyEvent>>,

    // The current autorepeat state.
    state: RefCell<State>,

    // The autorepeat settings.
    settings: Settings,

    // The task that feeds input events into the processing loop.
    _event_feeder: Task<()>,
}

impl Autorepeater {
    /// Creates a new [Autorepeater].  The `source` is a receiver end through which
    /// the input pipeline events are sent.  You must submit [Autorepeater::run]
    /// to an executor to start the event processing.
    pub fn new(source: UnboundedReceiver<InputEvent>) -> Rc<Self> {
        Self::new_with_settings(source, Default::default())
    }

    fn new_with_settings(
        mut source: UnboundedReceiver<InputEvent>,
        settings: Settings,
    ) -> Rc<Self> {
        let (event_sink, event_source) = mpsc::unbounded();

        // We need a task to feed input events into the channel read by `run()`.
        // The task will run until there is at least one sender. When there
        // are no more senders, `source.next().await` will return None, and
        // this task will exit.  The task will close the `event_sink` to
        // signal to the other end that it will send no more events, which can
        // be used for orderly shutdown.
        let event_feeder = {
            let event_sink = event_sink.clone();
            Task::local(async move {
                while let Some(event) = source.next().await {
                    match event {
                        InputEvent {
                            device_event: InputDeviceEvent::Keyboard(k),
                            device_descriptor,
                            event_time,
                            handled,
                        } if handled == Handled::No => unbounded_send_logged(
                            &event_sink,
                            AnyEvent::Keyboard(k, device_descriptor, event_time, handled),
                        )
                        .context("while forwarding a keyboard event"),
                        InputEvent {
                            device_event: _,
                            device_descriptor: _,
                            event_time: _,
                            handled: _,
                        } => unbounded_send_logged(&event_sink, AnyEvent::NonKeyboard(event))
                            .context("while forwarding a non-keyboard event"),
                    }
                    .unwrap_or_else(|e| fx_log_err!("could not run autorepeat: {:?}", e));
                }
                event_sink.close_channel();
            })
        };

        Rc::new(Autorepeater {
            event_sink,
            event_source: RefCell::new(event_source),
            state: RefCell::new(Default::default()),
            settings,
            _event_feeder: event_feeder,
        })
    }

    /// Run this function in an executor to start processing events. The
    /// transformed event stream is available in `output`.
    pub async fn run(self: &Rc<Self>, output: UnboundedSender<InputEvent>) -> Result<()> {
        fx_log_info!("key autorepeater installed");
        let src = &mut *(self.event_source.borrow_mut());
        while let Some(event) = src.next().await {
            match event {
                // Anything not a keyboard or any handled event gets forwarded as is.
                AnyEvent::NonKeyboard(input_event) => unbounded_send_logged(&output, input_event)?,
                AnyEvent::Keyboard(_, _, _, _) | AnyEvent::Timeout => {
                    self.process_event(event, &output).await?
                }
            }
        }
        // If we got to here, that means `src` was closed.
        // Ensure that the channel closure is propagated correctly.
        output.close_channel();

        // In production we never expect `src` to close as the autorepeater
        // should be operating continuously, so if we're here and we're in prod
        // this is unexpected. That is why we return an error.
        //
        // But, in tests it is acceptable to ignore this error and let the
        // function return.  An orderly shutdown will result.
        Err(anyhow!("recv loop is never supposed to terminate"))
    }

    // Replace the autorepeater state with a new one.
    fn set_state(self: &Rc<Self>, state: State) {
        fx_log_debug!("set state: {:?}", &state);
        self.state.replace(state);
    }

    // Get a copy of the current autorepeater state.
    fn get_state(self: &Rc<Self>) -> State {
        self.state.borrow().clone()
    }

    // Process a single `event`.  Any forwarded or generated events are emitted
    // into `output.
    async fn process_event(
        self: &Rc<Self>,
        event: AnyEvent,
        output: &UnboundedSender<InputEvent>,
    ) -> Result<()> {
        let old_state = self.get_state();
        fx_log_debug!("process_event: current state: {:?}", &old_state);
        fx_log_debug!("process_event: inbound event: {:?}", &event);
        match (old_state, event.clone()) {
            // This is the initial state.  We wait for a key event with a printable
            // character, since those are autorepeatable.
            (State::Dormant, AnyEvent::Keyboard(ev, descriptor, ..)) => {
                match (ev.get_event_type(), repeatability(ev.get_key_meaning())) {
                    // Only a printable key is a candidate for repeating.
                    // We start a delay timer and go to waiting.
                    (KeyEventType::Pressed, Repeatability::Yes) => {
                        let _delay_timer =
                            new_autorepeat_timer(self.event_sink.clone(), self.settings.delay);
                        self.set_state(State::Armed {
                            armed_event: ev,
                            armed_descriptor: descriptor,
                            _delay_timer,
                        });
                    }

                    // Any other key type or key event does not get repeated.
                    (_, _) => {}
                }
                unbounded_send_logged(&output, event.try_into()?)?;
            }

            // A timeout comes while we are in dormant state.  We expect
            // no timeouts in this state. Perhaps this is a timer task
            // that fired after it was canceled?  In any case, do not act on it,
            // but issue a warning.
            (State::Dormant, AnyEvent::Timeout) => {
                // This is unexpected, but not fatal.  If you see this in the
                // logs, we probably need to revisit the fuchsia_async::Task
                // semantics.
                fx_log_warn!("spurious timeout in the autorepeater");
            }

            // A keyboard event comes in while autorepeat is armed.
            //
            // If the keyboard event comes in about the same key that was armed, we
            // restart the repeat timer, to ensure repeated keypresses on the
            // same key don't generate even more repetitions.
            //
            // If the keyboard event is about a different repeatable key, we
            // restart the autorepeat timer with the new key.  This means that
            // an autorepeated sequence 'aaaaaabbbbbb' will pause for an
            // additional repeat delay between the last 'a' and the first 'b'
            // in the sequence.
            //
            // In all cases, pass the event onwards.  No events are dropped
            // from the event stream.
            (State::Armed { armed_event, .. }, AnyEvent::Keyboard(ev, descriptor, ..)) => {
                let ev = ev.clone();
                match (ev.get_event_type(), repeatability(ev.get_key_meaning())) {
                    (KeyEventType::Pressed, Repeatability::Yes) => {
                        let _delay_timer =
                            new_autorepeat_timer(self.event_sink.clone(), self.settings.delay);
                        self.set_state(State::Armed {
                            armed_event: ev,
                            armed_descriptor: descriptor,
                            _delay_timer,
                        });
                    }

                    (KeyEventType::Released, Repeatability::Yes) => {
                        // If the armed key was released, stop autorepeat.
                        // If the release was for another key, remain in the
                        // armed state.
                        if KeyboardEvent::same_key(&armed_event, &ev) {
                            self.set_state(State::Dormant);
                        }
                    }

                    // Any other event causes nothing special to happen.
                    _ => {}
                }
                unbounded_send_logged(&output, event.try_into()?)?;
            }

            // The timeout triggered while we are armed.  This is an autorepeat!
            (State::Armed { armed_event, armed_descriptor, .. }, AnyEvent::Timeout) => {
                let _delay_timer =
                    new_autorepeat_timer(self.event_sink.clone(), self.settings.period);
                let new_event = armed_event
                    .clone()
                    .into_with_repeat_sequence(armed_event.get_repeat_sequence() + 1);
                let new_event_time = input_device::event_time_or_now(None);

                self.set_state(State::Armed {
                    armed_event: new_event.clone(),
                    armed_descriptor: armed_descriptor.clone(),
                    _delay_timer,
                });
                // Generate a new autorepeat event and ship it out.
                let autorepeat_event =
                    AnyEvent::Keyboard(new_event, armed_descriptor, new_event_time, Handled::No);
                unbounded_send_logged(&output, autorepeat_event.try_into()?)?;
            }

            // Forward all other events unmodified.
            (_, AnyEvent::NonKeyboard(event)) => {
                unbounded_send_logged(&output, event)?;
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing_utilities;
    use fidl_fuchsia_input::Key;
    use fuchsia_async::TestExecutor;
    use fuchsia_zircon as zx;
    use futures::Future;
    use pin_utils::pin_mut;
    use pretty_assertions::assert_eq;
    use std::task::Poll;

    // Default autorepeat settings used for test.  If these settings are changed,
    // any tests may fail, since the tests are tuned to the precise timing that
    // is set here.
    fn default_settings() -> Settings {
        Settings { delay: Duration::from_millis(500), period: Duration::from_seconds(1) }
    }

    // Creates a new keyboard event for testing.
    fn new_event(
        key: Key,
        event_type: KeyEventType,
        key_meaning: Option<KeyMeaning>,
        repeat_sequence: u32,
    ) -> InputEvent {
        testing_utilities::create_keyboard_event_with_key_meaning_and_repeat_sequence(
            key,
            event_type,
            /*modifiers=*/ None,
            /*event_time*/ zx::Time::ZERO,
            &InputDeviceDescriptor::Fake,
            /*keymap=*/ None,
            key_meaning,
            repeat_sequence,
        )
    }

    fn new_handled_event(
        key: Key,
        event_type: KeyEventType,
        key_meaning: Option<KeyMeaning>,
        repeat_sequence: u32,
    ) -> InputEvent {
        let event = new_event(key, event_type, key_meaning, repeat_sequence);
        // Somewhat surprisingly, this works.
        InputEvent { handled: Handled::Yes, ..event }
    }

    // A shorthand for blocking the specified number of milliseconds, asynchronously.
    async fn wait_for_millis(millis: i64) {
        wait_for_duration(zx::Duration::from_millis(millis)).await;
    }

    async fn wait_for_duration(duration: Duration) {
        fuchsia_async::Timer::new(Time::after(duration)).await;
    }

    // Strip event time for these events, for comparison.  The event times are
    // unpredictable since they are read off of the real time monotonic clock,
    // and will be different in every run.
    fn remove_event_time(events: Vec<InputEvent>) -> Vec<InputEvent> {
        events
            .into_iter()
            .map(|InputEvent { device_event, device_descriptor, event_time: _, handled }| {
                InputEvent { device_event, device_descriptor, event_time: zx::Time::ZERO, handled }
            })
            .collect()
    }

    // Wait for this long (in fake time) before asserting events to ensure
    // that all events have been drained and all timers have fired.
    const SLACK_DURATION: Duration = zx::Duration::from_millis(5000);

    // Checks whether the events read from `output` match supplied `expected` events.
    async fn assert_events(output: UnboundedReceiver<InputEvent>, expected: Vec<InputEvent>) {
        // Spends a little while longer in the processing loop to ensure that all events have been
        // drained before we take the events out.  The wait is in fake time, so it does not
        // introduce nondeterminism into the tests.
        wait_for_duration(SLACK_DURATION).await;
        assert_eq!(
            remove_event_time(output.take(expected.len()).collect::<Vec<InputEvent>>().await),
            expected
        );
    }

    // Run the future `main_fut` in fake time.  The fake time is being advanced
    // in relatively small increments until a specified `total_duration` has
    // elapsed.
    //
    // This complication is needed to ensure that any expired
    // timers are awoken in the correct sequence because the test executor does
    // not automatically wake the timers. For the fake time execution to
    // be comparable to a real time execution, we need each timer to have the
    // chance of waking up, so that we can properly process the consequences
    // of that timer firing.
    //
    // We require that `main_fut` has completed at `total_duration`, and panic
    // if it has not.  This ensures that we never block forever in fake time.
    //
    // This method could possibly be implemented in [TestExecutor] for those
    // test executor users who do not care to wake the timers in any special
    // way.
    fn run_in_fake_time<F>(executor: &mut TestExecutor, main_fut: &mut F, total_duration: Duration)
    where
        F: Future<Output = ()> + Unpin,
    {
        const INCREMENT: Duration = zx::Duration::from_millis(13);
        // Run the loop for a bit longer than the fake time needed to pump all
        // the events, to allow the event queue to drain.
        let total_duration = total_duration + SLACK_DURATION;
        let mut current = zx::Duration::from_millis(0);
        let mut poll_status = Poll::Pending;

        // We run until either the future completes or the timeout is reached,
        // whichever comes first.
        // Running the future after it returns Poll::Ready is not allowed, so
        // we must exit the loop then.
        while current < total_duration && poll_status == Poll::Pending {
            executor.set_fake_time(Time::after(INCREMENT));
            executor.wake_expired_timers();
            poll_status = executor.run_until_stalled(main_fut);
            current = current + INCREMENT;
        }
        assert_eq!(
            poll_status,
            Poll::Ready(()),
            "the main future did not complete, perhaps increase total_duration?"
        );
    }

    // A general note for all the tests here.
    //
    // The autorepeat generator is tightly coupled with the real time clock. Such
    // a system would be hard to test robustly if we relied on the passage of
    // real time, since we can not do precise pauses, and are sensitive to
    // the scheduling delays in the emulators that run the tests.
    //
    // Instead, we use an executor with fake time: we have to advance the fake
    // time manually, and poke the executor such that we eventually run through
    // the predictable sequence of scheduled asynchronous events.
    //
    // The first few tests in this suite will have extra comments that explain
    // the general testing techniques.  Repeated uses of the same techniques
    // will not be specially pointed out in the later tests.

    // This test pushes a regular key press and release through the autorepeat
    // handler. It is used more to explain how the event processing works, than
    // it is exercising a specific feature of the autorepeat handler.
    #[test]
    fn basic_press_and_release_only() {
        // TestExecutor puts itself as the thread local executor. Any local
        // task spawned from here on will run on the test executor in fake time,
        // and will need `run_with_fake_time` to drive it to completion.
        let mut executor = TestExecutor::new_with_fake_time().unwrap();

        // `input` is where the test fixture will inject the fake input events.
        // `receiver` is where the autorepeater will read these events from.
        let (input, receiver) = mpsc::unbounded();

        // The autorepeat handler takes a receiver end of one channel to get
        // the input from, and the send end of another channel to place the
        // output into. Since we must formally start the handling process in
        // an async task, the API requires you to call `run` to
        // start the process and supply the sender side of the output.
        //
        // This API ensures that the handler is fully configured when started,
        // all the while leaving the user with an option of when and how exactly
        // to start the handler, including not immediately upon creation.
        let handler = Autorepeater::new_with_settings(receiver, default_settings());

        // `sender` is where the autorepeat handler will send processed input
        // events into.  `output` is where we will read the results of the
        // autorepeater's work.
        let (sender, output) = mpsc::unbounded();

        // It is up to the caller to decide where to spawn the handler task.
        let handler_task = Task::local(async move { handler.run(sender).await });

        // `main_fut` is the task that exercises the handler.
        let main_fut = async move {
            // Inject a keyboard event into the autorepeater.
            //
            // The mpsc channel of which 'input' is the receiver will be closed when all consumers
            // go out of scope.
            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Pressed,
                    Some(KeyMeaning::Codepoint('a' as u32)),
                    0,
                ))
                .unwrap();

            // This will wait in fake time.  The tests are not actually delayed because of this
            // call.
            wait_for_millis(1).await;

            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint('a' as u32)),
                    0,
                ))
                .unwrap();

            // Assertions are also here in the async domain since reading from
            // output must be asynchronous.
            assert_events(
                output,
                vec![
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        0,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        0,
                    ),
                ],
            )
            .await;
        };

        // Drive both the test fixture task and the handler task in parallel,
        // and both in fake time.  `run_in_fake_time` advances the fake time from
        // zero in increments of about 10ms until all futures complete.
        let joined_fut = Task::local(async move {
            let _r = futures::join!(handler_task, main_fut);
        });
        pin_mut!(joined_fut);
        run_in_fake_time(&mut executor, &mut joined_fut, zx::Duration::from_seconds(10));
    }

    // Ensures that we forward but not act on handled events.
    #[test]
    fn handled_events_are_forwarded() {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();
        let (input, receiver) = mpsc::unbounded();
        let handler = Autorepeater::new_with_settings(receiver, default_settings());
        let (sender, output) = mpsc::unbounded();
        let handler_task = Task::local(async move { handler.run(sender).await });

        let main_fut = async move {
            input
                .unbounded_send(new_handled_event(
                    Key::A,
                    KeyEventType::Pressed,
                    Some(KeyMeaning::Codepoint('a' as u32)),
                    0,
                ))
                .unwrap();

            wait_for_millis(2000).await;

            input
                .unbounded_send(new_handled_event(
                    Key::A,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint('a' as u32)),
                    0,
                ))
                .unwrap();

            assert_events(
                output,
                vec![
                    new_handled_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        0,
                    ),
                    new_handled_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        0,
                    ),
                ],
            )
            .await;
        };

        let joined_fut = Task::local(async move {
            let _r = futures::join!(handler_task, main_fut);
        });
        pin_mut!(joined_fut);
        run_in_fake_time(&mut executor, &mut joined_fut, zx::Duration::from_seconds(10));
    }

    // In this test, we wait with a pressed key for long enough that the default
    // settings should trigger the autorepeat.
    #[test]
    fn autorepeat_simple() {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();

        let (input, receiver) = mpsc::unbounded();
        let handler = Autorepeater::new_with_settings(receiver, default_settings());
        let (sender, output) = mpsc::unbounded();
        let handler_task = Task::local(async move { handler.run(sender).await });

        let main_fut = async move {
            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Pressed,
                    Some(KeyMeaning::Codepoint('a' as u32)),
                    0,
                ))
                .unwrap();

            wait_for_millis(2000).await;

            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint('a' as u32)),
                    0,
                ))
                .unwrap();

            // The total fake time during which the autorepeat key was actuated
            // was 2 seconds.  By default the delay to first autorepeat is 500ms,
            // then 1000ms for each additional autorepeat. This means we should
            // see three `Pressed` events: one at the outset, a second one after
            // 500ms, and a third one after 1500ms.
            assert_events(
                output,
                vec![
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        0,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        1,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        2,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        0,
                    ),
                ],
            )
            .await;
        };
        let joined_fut = Task::local(async move {
            let _r = futures::join!(handler_task, main_fut);
        });
        pin_mut!(joined_fut);
        run_in_fake_time(&mut executor, &mut joined_fut, zx::Duration::from_seconds(10));
    }

    // This test is the same as above, but we hold the autorepeat for a little
    // while longer and check that the autorepeat event stream has grown
    // accordingly.
    #[test]
    fn autorepeat_simple_longer() {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();

        let (input, receiver) = mpsc::unbounded();
        let handler = Autorepeater::new_with_settings(receiver, default_settings());
        let (sender, output) = mpsc::unbounded();
        let handler_task = Task::local(async move { handler.run(sender).await });

        let main_fut = async move {
            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Pressed,
                    Some(KeyMeaning::Codepoint('a' as u32)),
                    0,
                ))
                .unwrap();

            wait_for_millis(3000).await;

            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint('a' as u32)),
                    0,
                ))
                .unwrap();

            assert_events(
                output,
                vec![
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        0,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        1,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        2,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        3,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        0,
                    ),
                ],
            )
            .await;
        };
        let joined_fut = Task::local(async move {
            let _r = futures::join!(handler_task, main_fut);
        });
        pin_mut!(joined_fut);
        run_in_fake_time(&mut executor, &mut joined_fut, zx::Duration::from_seconds(10));
    }

    // In this test, keys A and B compete for autorepeat:
    //
    //     @0ms ->|<- 1.6s ->|<- 2s ->|<- 1s ->|
    // A """""""""\___________________/"""""""""""""
    //            :          :                 :
    // B """"""""""""""""""""\_________________/""""
    #[test]
    fn autorepeat_takeover() {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();

        let (input, receiver) = mpsc::unbounded();
        let handler = Autorepeater::new_with_settings(receiver, default_settings());
        let (sender, output) = mpsc::unbounded();
        let handler_task = Task::local(async move { handler.run(sender).await });

        let main_fut = async move {
            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Pressed,
                    Some(KeyMeaning::Codepoint('a' as u32)),
                    0,
                ))
                .unwrap();

            wait_for_millis(1600).await;

            input
                .unbounded_send(new_event(
                    Key::B,
                    KeyEventType::Pressed,
                    Some(KeyMeaning::Codepoint('b' as u32)),
                    0,
                ))
                .unwrap();

            wait_for_millis(2000).await;

            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint('a' as u32)),
                    0,
                ))
                .unwrap();

            wait_for_millis(1000).await;

            input
                .unbounded_send(new_event(
                    Key::B,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint('b' as u32)),
                    0,
                ))
                .unwrap();

            assert_events(
                output,
                vec![
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        0,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        1,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        2,
                    ),
                    new_event(
                        Key::B,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('b' as u32)),
                        0,
                    ),
                    new_event(
                        Key::B,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('b' as u32)),
                        1,
                    ),
                    new_event(
                        Key::B,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('b' as u32)),
                        2,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        0,
                    ),
                    new_event(
                        Key::B,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('b' as u32)),
                        3,
                    ),
                    new_event(
                        Key::B,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('b' as u32)),
                        0,
                    ),
                ],
            )
            .await;
        };
        let joined_fut = Task::local(async move {
            let _r = futures::join!(handler_task, main_fut);
        });
        pin_mut!(joined_fut);
        run_in_fake_time(&mut executor, &mut joined_fut, zx::Duration::from_seconds(10));
    }

    // In this test, keys A and B compete for autorepeat:
    //
    //     @0ms ->|<- 2s ->|<- 2s ->|<- 2s ->|
    // A """""""""\__________________________/""""
    //            :        :        :        :
    // B """"""""""""""""""\________/"""""""""""""
    #[test]
    fn autorepeat_takeover_and_back() {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();

        let (input, receiver) = mpsc::unbounded();
        let handler = Autorepeater::new_with_settings(receiver, default_settings());
        let (sender, output) = mpsc::unbounded();
        let handler_task = Task::local(async move { handler.run(sender).await });

        let main_fut = async move {
            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Pressed,
                    Some(KeyMeaning::Codepoint('a' as u32)),
                    0,
                ))
                .unwrap();

            wait_for_millis(2000).await;

            input
                .unbounded_send(new_event(
                    Key::B,
                    KeyEventType::Pressed,
                    Some(KeyMeaning::Codepoint('b' as u32)),
                    0,
                ))
                .unwrap();

            wait_for_millis(2000).await;

            input
                .unbounded_send(new_event(
                    Key::B,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint('b' as u32)),
                    0,
                ))
                .unwrap();

            wait_for_millis(2000).await;

            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint('a' as u32)),
                    0,
                ))
                .unwrap();

            // Try to elicit autorepeat.  There won't be any.
            wait_for_millis(2000).await;

            assert_events(
                output,
                vec![
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        0,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        1,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        2,
                    ),
                    new_event(
                        Key::B,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('b' as u32)),
                        0,
                    ),
                    new_event(
                        Key::B,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('b' as u32)),
                        1,
                    ),
                    new_event(
                        Key::B,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('b' as u32)),
                        2,
                    ),
                    new_event(
                        Key::B,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('b' as u32)),
                        0,
                    ),
                    // No autorepeat after B is released.
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('a' as u32)),
                        0,
                    ),
                ],
            )
            .await;
        };
        let joined_fut = Task::local(async move {
            let _r = futures::join!(handler_task, main_fut);
        });
        pin_mut!(joined_fut);
        run_in_fake_time(&mut executor, &mut joined_fut, zx::Duration::from_seconds(10));
    }

    #[test]
    fn no_autorepeat_for_left_shift() {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();

        let (input, receiver) = mpsc::unbounded();
        let handler = Autorepeater::new_with_settings(receiver, default_settings());
        let (sender, output) = mpsc::unbounded();
        let handler_task = Task::local(async move { handler.run(sender).await });

        let main_fut = async move {
            input
                .unbounded_send(new_event(
                    Key::LeftShift,
                    KeyEventType::Pressed,
                    // Keys that do not contribute to text editing have code
                    // point set to zero. We use this as a discriminator for
                    // which keys may or may not repeat.
                    Some(KeyMeaning::Codepoint(0)),
                    0,
                ))
                .unwrap();

            wait_for_millis(5000).await;

            input
                .unbounded_send(new_event(
                    Key::LeftShift,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint(0)),
                    0,
                ))
                .unwrap();

            assert_events(
                output,
                vec![
                    new_event(
                        Key::LeftShift,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(0)),
                        0,
                    ),
                    new_event(
                        Key::LeftShift,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(0)),
                        0,
                    ),
                ],
            )
            .await;
        };
        let joined_fut = Task::local(async move {
            let _r = futures::join!(handler_task, main_fut);
        });
        pin_mut!(joined_fut);
        run_in_fake_time(&mut executor, &mut joined_fut, zx::Duration::from_seconds(10));
    }

    //       @0ms ->|<- 2s ->|<- 2s ->|<- 2s ->|
    // A         """"""""""""\________/"""""""""""""
    // LeftShift """\__________________________/""""
    #[test]
    fn shift_a_encapsulated() {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();

        let (input, receiver) = mpsc::unbounded();
        let handler = Autorepeater::new_with_settings(receiver, default_settings());
        let (sender, output) = mpsc::unbounded();
        let handler_task = Task::local(async move { handler.run(sender).await });

        let main_fut = async move {
            input
                .unbounded_send(new_event(
                    Key::LeftShift,
                    KeyEventType::Pressed,
                    Some(KeyMeaning::Codepoint(0)),
                    0,
                ))
                .unwrap();

            wait_for_millis(2000).await;

            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Pressed,
                    Some(KeyMeaning::Codepoint('A' as u32)),
                    0,
                ))
                .unwrap();

            wait_for_millis(2000).await;

            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint('A' as u32)),
                    0,
                ))
                .unwrap();

            wait_for_millis(2000).await;

            input
                .unbounded_send(new_event(
                    Key::LeftShift,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint(0)),
                    0,
                ))
                .unwrap();

            assert_events(
                output,
                vec![
                    new_event(
                        Key::LeftShift,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(0)),
                        0,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                        0,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                        1,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                        2,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                        0,
                    ),
                    new_event(
                        Key::LeftShift,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(0)),
                        0,
                    ),
                ],
            )
            .await;
        };
        let joined_fut = Task::local(async move {
            let _r = futures::join!(handler_task, main_fut);
        });
        pin_mut!(joined_fut);
        run_in_fake_time(&mut executor, &mut joined_fut, zx::Duration::from_seconds(10));
    }

    //       @0ms ->|<- 2s ->|<- 2s ->|<- 2s ->|
    // A         """"""""""""\_________________/""
    // LeftShift """\_________________/"""""""""""
    #[test]
    fn shift_a_interleaved() {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();

        let (input, receiver) = mpsc::unbounded();
        let handler = Autorepeater::new_with_settings(receiver, default_settings());
        let (sender, output) = mpsc::unbounded();
        let handler_task = Task::local(async move { handler.run(sender).await });

        let main_fut = async move {
            input
                .unbounded_send(new_event(
                    Key::LeftShift,
                    KeyEventType::Pressed,
                    Some(KeyMeaning::Codepoint(0)),
                    0,
                ))
                .unwrap();

            wait_for_millis(2000).await;

            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Pressed,
                    Some(KeyMeaning::Codepoint('A' as u32)),
                    0,
                ))
                .unwrap();

            wait_for_millis(2000).await;

            input
                .unbounded_send(new_event(
                    Key::LeftShift,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint(0)),
                    0,
                ))
                .unwrap();

            wait_for_millis(2000).await;

            input
                .unbounded_send(new_event(
                    Key::A,
                    KeyEventType::Released,
                    Some(KeyMeaning::Codepoint('A' as u32)),
                    0,
                ))
                .unwrap();

            assert_events(
                output,
                vec![
                    new_event(
                        Key::LeftShift,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint(0)),
                        0,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                        0,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                        1,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                        2,
                    ),
                    new_event(
                        Key::LeftShift,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint(0)),
                        0,
                    ),
                    // This will continue autorepeating capital A, but we'd need
                    // to autorepeat 'a'.  May need to reapply the keymap at
                    // this point, but this may require redoing the keymap stage.
                    // Alternative - stop autorepeat, would be easier.
                    // The current behavior may be enough, however.
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                        3,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Pressed,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                        4,
                    ),
                    new_event(
                        Key::A,
                        KeyEventType::Released,
                        Some(KeyMeaning::Codepoint('A' as u32)),
                        0,
                    ),
                ],
            )
            .await;
        };
        let joined_fut = async move {
            let _r = futures::join!(main_fut, handler_task);
        };
        pin_mut!(joined_fut);
        run_in_fake_time(&mut executor, &mut joined_fut, zx::Duration::from_seconds(10));
    }
}
