// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::input_device::{self, InputEvent};
use crate::keyboard_binding::{KeyboardDeviceDescriptor, KeyboardEvent};
use anyhow::{Context, Result};
use fidl_fuchsia_ui_input3::KeyEventType;
use fidl_fuchsia_ui_scenic as fscenic;
use fuchsia_async::{OnSignals, Task};
use fuchsia_syslog::{fx_log_debug, fx_log_info, fx_log_warn};
use fuchsia_zircon::{AsHandleRef, Duration, Signals, Status, Time};
use futures::{
    channel::mpsc::{self, UnboundedReceiver, UnboundedSender},
    select, StreamExt,
};
use keymaps::KeyState;
use lazy_static::lazy_static;
use std::{cell::RefCell, rc::Rc};

lazy_static! {
    // The signal value corresponding to the `DISPLAY_OWNED_SIGNAL`.  Same as zircon's signal
    // USER_0.
    static ref DISPLAY_OWNED: Signals = Signals::from_bits(fscenic::DISPLAY_OWNED_SIGNAL)
        .expect("static init should not fail")    ;

    // The signal value corresponding to the `DISPLAY_NOT_OWNED_SIGNAL`.  Same as zircon's signal
    // USER_1.
    static ref DISPLAY_UNOWNED: Signals = Signals::from_bits(fscenic::DISPLAY_NOT_OWNED_SIGNAL)
        .expect("static init should not fail")    ;

    // Any display-related signal.
    static ref ANY_DISPLAY_EVENT: Signals = *DISPLAY_OWNED | *DISPLAY_UNOWNED;
}

// Stores the last received ownership signals.
#[derive(Debug, Clone, PartialEq)]
struct Ownership {
    signals: Signals,
}

impl std::convert::From<Signals> for Ownership {
    fn from(signals: Signals) -> Self {
        Ownership { signals }
    }
}

impl Ownership {
    // Returns true if the display is currently indicated to be not owned by
    // Scenic.
    fn is_display_ownership_lost(&self) -> bool {
        self.signals.contains(*DISPLAY_UNOWNED)
    }

    // Returns the mask of the next signal to watch.
    //
    // Since the ownership alternates, so does the next signal to wait on.
    fn next_signal(&self) -> Signals {
        match self.is_display_ownership_lost() {
            true => *DISPLAY_OWNED,
            false => *DISPLAY_UNOWNED,
        }
    }

    /// Waits for the next signal change.
    ///
    /// If the display is owned, it will wait for display to become unowned.
    /// If the display is unowned, it will wait for the display to become owned.
    async fn wait_ownership_change<'a, T: AsHandleRef>(
        &self,
        event: &'a T,
    ) -> Result<Signals, Status> {
        OnSignals::new(event, self.next_signal()).await
    }
}

/// A handler that turns the input pipeline off or on based on whether
/// the Scenic owns the display.
///
/// This allows us to turn off keyboard processing when the user switches away
/// from the product (e.g. terminal) into virtual console.
///
/// See the `README.md` file in this crate for details.
pub struct DisplayOwnership {
    /// The current view of the display ownership.  It is mutated by the
    /// display ownership task when appropriate signals arrive.
    ownership: Rc<RefCell<Ownership>>,

    /// The registry of currently pressed keys.
    key_state: RefCell<KeyState>,

    /// The source of ownership change events for the main loop.
    display_ownership_change_receiver: RefCell<UnboundedReceiver<Ownership>>,

    /// A background task that watches for display ownership changes.  We keep
    /// it alive to ensure that it keeps running.
    _display_ownership_task: Task<()>,

    /// The event processing loop will do an `unbounded_send(())` on this
    /// channel once at the end of each loop pass, in test configurations only.
    /// The test fixture uses this channel to execute test fixture in
    /// lock-step with the event processing loop for test cases where the
    /// precise event sequencing is relevant.
    #[cfg(test)]
    loop_done: RefCell<Option<UnboundedSender<()>>>,
}

impl DisplayOwnership {
    /// Creates a new handler that watches `display_ownership_event` for events.
    ///
    /// The `display_ownership_event` is assumed to be an [Event] obtained from
    /// Scenic using `fuchsia.ui.scenic.Scenic/GetDisplayOwnershipEvent`.  There
    /// isn't really a way for this code to know here whether this is true or
    /// not, so implementor beware.
    pub fn new(display_ownership_event: impl AsHandleRef + 'static) -> Rc<Self> {
        DisplayOwnership::new_internal(display_ownership_event, None)
    }

    #[cfg(test)]
    pub fn new_for_test(
        display_ownership_event: impl AsHandleRef + 'static,
        loop_done: UnboundedSender<()>,
    ) -> Rc<Self> {
        DisplayOwnership::new_internal(display_ownership_event, Some(loop_done))
    }

    fn new_internal(
        display_ownership_event: impl AsHandleRef + 'static,
        _loop_done: Option<UnboundedSender<()>>,
    ) -> Rc<Self> {
        let initial_state = display_ownership_event
            // scenic guarantees that ANY_DISPLAY_EVENT is asserted. If it is
            // not, this will fail with a timeout error.
            .wait_handle(*ANY_DISPLAY_EVENT, Time::INFINITE_PAST)
            .expect("unable to set the initial display state");
        fx_log_debug!("setting initial display ownership to: {:?}", &initial_state);
        let initial_ownership: Ownership = initial_state.into();
        let ownership = Rc::new(RefCell::new(initial_ownership.clone()));

        let mut ownership_clone = initial_ownership.clone();
        let (ownership_sender, ownership_receiver) = mpsc::unbounded();
        let display_ownership_task = Task::local(async move {
            loop {
                let signals = ownership_clone.wait_ownership_change(&display_ownership_event).await;
                match signals {
                    Err(e) => {
                        fx_log_warn!("could not read display state: {:?}", e);
                        break;
                    }
                    Ok(signals) => {
                        fx_log_debug!("setting display ownership to: {:?}", &signals);
                        ownership_sender.unbounded_send(signals.into()).unwrap();
                        ownership_clone = signals.into();
                    }
                }
            }
            fx_log_warn!("display loop exiting and will no longer monitor display changes - this is not expected");
        });
        fx_log_info!("Display ownership handler installed");
        Rc::new(Self {
            ownership,
            key_state: RefCell::new(KeyState::new()),
            display_ownership_change_receiver: RefCell::new(ownership_receiver),
            _display_ownership_task: display_ownership_task,
            #[cfg(test)]
            loop_done: RefCell::new(_loop_done),
        })
    }

    /// Returns true if the display is currently *not* owned by Scenic.
    fn is_display_ownership_lost(&self) -> bool {
        self.ownership.borrow().is_display_ownership_lost()
    }

    /// Run this function in an executor to handle events.
    pub async fn handle_input_events(
        self: &Rc<Self>,
        mut input: UnboundedReceiver<InputEvent>,
        output: UnboundedSender<InputEvent>,
    ) -> Result<()> {
        loop {
            let mut ownership_source = self.display_ownership_change_receiver.borrow_mut();
            select! {
                // Display ownership changed.
                new_ownership = ownership_source.select_next_some() => {
                    let is_display_ownership_lost = new_ownership.is_display_ownership_lost();
                    // When the ownership is modified, float a set of cancel or sync
                    // events to scoop up stale keyboard state, treating it the same
                    // as loss of focus.
                    let event_type = match is_display_ownership_lost {
                        true => KeyEventType::Cancel,
                        false => KeyEventType::Sync,
                    };
                    let keys = self.key_state.borrow().get_set();
                    let mut event_time = Time::get_monotonic();
                    for key in keys.into_iter() {
                        let key_event = KeyboardEvent::new(key, event_type);
                        output.unbounded_send(into_input_event(key_event, event_time))
                            .context("unable to send display updates")?;
                        event_time = event_time + Duration::from_nanos(1);
                    }
                    *(self.ownership.borrow_mut()) = new_ownership;
                },

                // An input event arrived.
                event = input.select_next_some() => {
                    if event.is_handled() {
                        // Forward handled events unmodified.
                        output.unbounded_send(event).context("unable to send handled event")?;
                        continue;
                    }
                    match event.device_event {
                        input_device::InputDeviceEvent::Keyboard(ref event) => {
                            self.key_state.borrow_mut().update(event.get_event_type(), event.get_key());
                        },
                        _ => {},
                    }
                    let is_display_ownership_lost = self.is_display_ownership_lost();
                    output.unbounded_send(
                        input_device::InputEvent::from(event)
                            .into_handled_if(is_display_ownership_lost)
                    ).context("unable to send input event updates")?;
                },
            };
            #[cfg(test)]
            {
                self.loop_done.borrow_mut().as_ref().unwrap().unbounded_send(()).unwrap();
            }
        }
    }
}

fn empty_keyboard_device_descriptor() -> input_device::InputDeviceDescriptor {
    input_device::InputDeviceDescriptor::Keyboard(
        // Should descriptor be something sensible?
        KeyboardDeviceDescriptor {
            keys: vec![],
            device_info: fidl_fuchsia_input_report::DeviceInfo {
                vendor_id: 0,
                product_id: 0,
                version: 0,
            },
            device_id: 0,
        },
    )
}

fn into_input_event(keyboard_event: KeyboardEvent, event_time: Time) -> input_device::InputEvent {
    input_device::InputEvent {
        device_event: input_device::InputDeviceEvent::Keyboard(keyboard_event),
        device_descriptor: empty_keyboard_device_descriptor(),
        event_time,
        handled: input_device::Handled::No,
        trace_id: None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::input_device::InputEvent;
    use crate::testing_utilities::{create_fake_input_event, create_input_event};
    use fidl_fuchsia_input::Key;
    use fidl_fuchsia_ui_input3::KeyEventType;
    use fuchsia_async as fasync;
    use fuchsia_zircon::{EventPair, Peered, Time};
    use futures::channel::mpsc;
    use pretty_assertions::assert_eq;

    // Manages losing and regaining display, since manual management is error-prone:
    // if signal_peer does not change the signal state, the waiting process will block
    // forever, which makes tests run longer than needed.
    struct DisplayWrangler {
        event: EventPair,
        last: Signals,
    }

    impl DisplayWrangler {
        fn new(event: EventPair) -> Self {
            let mut instance = DisplayWrangler { event, last: *DISPLAY_OWNED };
            // Signal needs to be initialized before the handlers attempts to read it.
            // This is normally always the case in production.
            // Else, the `new_for_test` below will panic with a TIMEOUT error.
            instance.set_unowned();
            instance
        }

        fn set_unowned(&mut self) {
            assert!(self.last != *DISPLAY_UNOWNED, "display is already unowned");
            self.event.signal_peer(*DISPLAY_OWNED, *DISPLAY_UNOWNED).unwrap();
            self.last = *DISPLAY_UNOWNED;
        }

        fn set_owned(&mut self) {
            assert!(self.last != *DISPLAY_OWNED, "display is already owned");
            self.event.signal_peer(*DISPLAY_UNOWNED, *DISPLAY_OWNED).unwrap();
            self.last = *DISPLAY_OWNED;
        }
    }

    #[fuchsia::test]
    async fn display_ownership_change() {
        // handler_event is the event that the unit under test will examine for
        // display ownership changes.  test_event is used to set the appropriate
        // signals.
        let (test_event, handler_event) = EventPair::create().unwrap();

        // test_sender is used to pipe input events into the handler.
        let (test_sender, handler_receiver) = mpsc::unbounded::<InputEvent>();

        // test_receiver is used to pipe input events out of the handler.
        let (handler_sender, test_receiver) = mpsc::unbounded::<InputEvent>();

        // The unit under test adds a () each time it completes one pass through
        // its event loop.  Use to ensure synchronization.
        let (loop_done_sender, mut loop_done) = mpsc::unbounded::<()>();

        // We use a wrapper to signal test_event correctly, since doing it wrong
        // by hand causes tests to hang, which isn't the best dev experience.
        let mut wrangler = DisplayWrangler::new(test_event);
        let handler = DisplayOwnership::new_for_test(handler_event, loop_done_sender);

        let _task = fasync::Task::local(async move {
            handler.handle_input_events(handler_receiver, handler_sender).await.unwrap();
        });

        let fake_time = Time::from_nanos(42);

        // Go two full circles of signaling.

        // 1
        wrangler.set_owned();
        loop_done.next().await;
        test_sender.unbounded_send(create_fake_input_event(fake_time)).unwrap();
        loop_done.next().await;

        // 2
        wrangler.set_unowned();
        loop_done.next().await;
        test_sender.unbounded_send(create_fake_input_event(fake_time)).unwrap();
        loop_done.next().await;

        // 3
        wrangler.set_owned();
        loop_done.next().await;
        test_sender.unbounded_send(create_fake_input_event(fake_time)).unwrap();
        loop_done.next().await;

        // 4
        wrangler.set_unowned();
        loop_done.next().await;
        test_sender.unbounded_send(create_fake_input_event(fake_time)).unwrap();
        loop_done.next().await;

        let actual: Vec<InputEvent> =
            test_receiver.take(4).map(|e| e.into_with_event_time(fake_time)).collect().await;

        assert_eq!(
            actual,
            vec![
                // Event received while we owned the display.
                create_fake_input_event(fake_time),
                // Event received when we lost the display.
                create_fake_input_event(fake_time).into_handled(),
                // Display ownership regained.
                create_fake_input_event(fake_time),
                // Display ownership lost.
                create_fake_input_event(fake_time).into_handled(),
            ]
        );
    }

    fn new_keyboard_input_event(key: Key, event_type: KeyEventType) -> InputEvent {
        let fake_time = Time::from_nanos(42);
        create_input_event(
            KeyboardEvent::new(key, event_type),
            &input_device::InputDeviceDescriptor::Fake,
            fake_time,
            input_device::Handled::No,
        )
    }

    #[fuchsia::test]
    async fn basic_key_state_handling() {
        let (test_event, handler_event) = EventPair::create().unwrap();
        let (test_sender, handler_receiver) = mpsc::unbounded::<InputEvent>();
        let (handler_sender, test_receiver) = mpsc::unbounded::<InputEvent>();
        let (loop_done_sender, mut loop_done) = mpsc::unbounded::<()>();
        let mut wrangler = DisplayWrangler::new(test_event);
        let handler = DisplayOwnership::new_for_test(handler_event, loop_done_sender);
        let _task = fasync::Task::local(async move {
            handler.handle_input_events(handler_receiver, handler_sender).await.unwrap();
        });

        let fake_time = Time::from_nanos(42);

        // Gain the display, and press a key.
        wrangler.set_owned();
        loop_done.next().await;
        test_sender
            .unbounded_send(new_keyboard_input_event(Key::A, KeyEventType::Pressed))
            .unwrap();
        loop_done.next().await;

        // Lose display.
        wrangler.set_unowned();
        loop_done.next().await;

        // Regain display
        wrangler.set_owned();
        loop_done.next().await;

        // Key event after regaining.
        test_sender
            .unbounded_send(new_keyboard_input_event(Key::A, KeyEventType::Released))
            .unwrap();
        loop_done.next().await;

        let actual: Vec<InputEvent> =
            test_receiver.take(4).map(|e| e.into_with_event_time(fake_time)).collect().await;

        assert_eq!(
            actual,
            vec![
                new_keyboard_input_event(Key::A, KeyEventType::Pressed),
                new_keyboard_input_event(Key::A, KeyEventType::Cancel)
                    .into_with_device_descriptor(empty_keyboard_device_descriptor()),
                new_keyboard_input_event(Key::A, KeyEventType::Sync)
                    .into_with_device_descriptor(empty_keyboard_device_descriptor()),
                new_keyboard_input_event(Key::A, KeyEventType::Released),
            ]
        );
    }

    #[fuchsia::test]
    async fn more_key_state_handling() {
        let (test_event, handler_event) = EventPair::create().unwrap();
        let (test_sender, handler_receiver) = mpsc::unbounded::<InputEvent>();
        let (handler_sender, test_receiver) = mpsc::unbounded::<InputEvent>();
        let (loop_done_sender, mut loop_done) = mpsc::unbounded::<()>();
        let mut wrangler = DisplayWrangler::new(test_event);
        let handler = DisplayOwnership::new_for_test(handler_event, loop_done_sender);
        let _task = fasync::Task::local(async move {
            handler.handle_input_events(handler_receiver, handler_sender).await.unwrap();
        });

        let fake_time = Time::from_nanos(42);

        wrangler.set_owned();
        loop_done.next().await;
        test_sender
            .unbounded_send(new_keyboard_input_event(Key::A, KeyEventType::Pressed))
            .unwrap();
        loop_done.next().await;
        test_sender
            .unbounded_send(new_keyboard_input_event(Key::B, KeyEventType::Pressed))
            .unwrap();
        loop_done.next().await;

        // Lose display, release a key, press a key.
        wrangler.set_unowned();
        loop_done.next().await;
        test_sender
            .unbounded_send(new_keyboard_input_event(Key::B, KeyEventType::Released))
            .unwrap();
        loop_done.next().await;
        test_sender
            .unbounded_send(new_keyboard_input_event(Key::C, KeyEventType::Pressed))
            .unwrap();
        loop_done.next().await;

        // Regain display
        wrangler.set_owned();
        loop_done.next().await;

        // Key event after regaining.
        test_sender
            .unbounded_send(new_keyboard_input_event(Key::A, KeyEventType::Released))
            .unwrap();
        loop_done.next().await;
        test_sender
            .unbounded_send(new_keyboard_input_event(Key::C, KeyEventType::Released))
            .unwrap();
        loop_done.next().await;

        let actual: Vec<InputEvent> =
            test_receiver.take(10).map(|e| e.into_with_event_time(fake_time)).collect().await;

        assert_eq!(
            actual,
            vec![
                new_keyboard_input_event(Key::A, KeyEventType::Pressed),
                new_keyboard_input_event(Key::B, KeyEventType::Pressed),
                new_keyboard_input_event(Key::A, KeyEventType::Cancel)
                    .into_with_device_descriptor(empty_keyboard_device_descriptor()),
                new_keyboard_input_event(Key::B, KeyEventType::Cancel)
                    .into_with_device_descriptor(empty_keyboard_device_descriptor()),
                new_keyboard_input_event(Key::B, KeyEventType::Released).into_handled(),
                new_keyboard_input_event(Key::C, KeyEventType::Pressed).into_handled(),
                // The CANCEL and SYNC events are emitted in the sort ordering of the
                // `Key` enum values. Perhaps they should be emitted instead in the order
                // they have been received for SYNC, and in reverse order for CANCEL.
                new_keyboard_input_event(Key::A, KeyEventType::Sync)
                    .into_with_device_descriptor(empty_keyboard_device_descriptor()),
                new_keyboard_input_event(Key::C, KeyEventType::Sync)
                    .into_with_device_descriptor(empty_keyboard_device_descriptor()),
                new_keyboard_input_event(Key::A, KeyEventType::Released),
                new_keyboard_input_event(Key::C, KeyEventType::Released),
            ]
        );
    }
}
