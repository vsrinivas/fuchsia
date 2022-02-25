// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::input_device;
use crate::input_handler::UnhandledInputHandler;
use crate::keyboard_binding::{KeyboardDeviceDescriptor, KeyboardEvent};
use async_trait::async_trait;
use fidl_fuchsia_ui_input3::KeyEventType;
use fidl_fuchsia_ui_scenic as fscenic;
use fuchsia_async::{OnSignals, Task};
use fuchsia_syslog::{fx_log_debug, fx_log_info, fx_log_warn};
use fuchsia_zircon::{AsHandleRef, Duration, Signals, Status, Time};
use futures::channel::mpsc::UnboundedSender;
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
    fn is_display_unowned(&self) -> bool {
        self.signals.contains(*DISPLAY_UNOWNED)
    }

    // Returns the mask of the next signal to watch.
    //
    // Since the ownership alternates, so does the next signal to wait on.
    fn next_signal(&self) -> Signals {
        match self.is_display_unowned() {
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
pub struct DisplayOwnershipHandler {
    /// The current view of the display ownership.  It is mutated by the
    /// display ownership task when appropriate signals arrive.
    ownership: Rc<RefCell<Ownership>>,

    /// The value of `ownership` in the previous pass through this event handler.
    previous_ownership: RefCell<Ownership>,

    /// The registry of currently pressed keys.
    key_state: RefCell<KeyState>,

    /// A background task that watches for display ownership changes.
    _display_ownership_task: Task<()>,
}

impl DisplayOwnershipHandler {
    /// Creates a new handler that watches `display_ownership_event` for events.
    ///
    /// The `display_ownership_event` is assumed to be an [Event] obtained from
    /// Scenic using `fuchsia.ui.scenic.Scenic/GetDisplayOwnershipEvent`.  There
    /// isn't really a way for this code to know here whether this is true or
    /// not, so implementor beware.
    pub fn new(display_ownership_event: impl AsHandleRef + 'static) -> Rc<Self> {
        DisplayOwnershipHandler::new_internal(display_ownership_event, None)
    }

    /// Create the handler for testing, and signal `sync` on each change.
    #[cfg(test)]
    pub(crate) fn new_for_test(
        display_ownership_event: impl AsHandleRef + 'static,
        sync: UnboundedSender<()>,
    ) -> Rc<Self> {
        DisplayOwnershipHandler::new_internal(display_ownership_event, Some(sync))
    }

    // If sync is present, it is signaled on each signal change.
    // Only to be used for synchronization in testing.
    fn new_internal(
        display_ownership_event: impl AsHandleRef + 'static,
        sync: Option<UnboundedSender<()>>,
    ) -> Rc<Self> {
        let initial_state = display_ownership_event
            // scenic guarantees that ANY_DISPLAY_EVENT is asserted. If it is
            // not, this will fail with a timeout error.
            .wait_handle(*ANY_DISPLAY_EVENT, Time::INFINITE_PAST)
            .expect("unable to set the initial display state");
        if let Some(ref channel) = sync {
            channel.unbounded_send(()).expect("init signaled with success");
        }

        fx_log_debug!("setting initial display ownership to: {:?}", &initial_state);
        let initial_ownership: Ownership = initial_state.into();
        let ownership = Rc::new(RefCell::new(initial_ownership.clone()));

        let ownership_clone = ownership.clone();
        let display_ownership_task = Task::local(async move {
            let sync_clone = move || sync.clone();
            loop {
                let signals = ownership_clone
                    .as_ref()
                    .borrow()
                    .wait_ownership_change(&display_ownership_event)
                    .await;
                match signals {
                    Err(e) => {
                        fx_log_warn!("could not read display state: {:?}", e);
                        break;
                    }
                    Ok(signals) => {
                        fx_log_debug!("setting display ownership to: {:?}", &signals);
                        *(ownership_clone.as_ref().borrow_mut()) = signals.into();
                        if let Some(ref channel) = sync_clone() {
                            channel.unbounded_send(()).expect("signaled with success");
                        }
                    }
                }
            }
            fx_log_warn!("display loop exiting and will no longer monitor display changes - this is not expected");
        });
        fx_log_info!("Display ownership handler installed");
        Rc::new(Self {
            ownership,
            previous_ownership: RefCell::new(initial_ownership),
            key_state: RefCell::new(KeyState::new()),
            _display_ownership_task: display_ownership_task,
        })
    }

    /// Returns true if the display is currently *not* owned by Scenic.
    fn is_display_unowned(&self) -> bool {
        self.ownership.as_ref().borrow().is_display_unowned()
    }

    fn update_previous_ownership(self: Rc<Self>) {
        *(self.previous_ownership.borrow_mut()) = self.ownership.borrow().clone();
    }

    /// Returns true if the previous ownership is the same as the current.
    fn is_ownership_modified(self: Rc<Self>) -> bool {
        *(self.previous_ownership.borrow()) != *(self.ownership.borrow())
    }
}

fn empty_keyboard_device_descriptor() -> input_device::InputDeviceDescriptor {
    input_device::InputDeviceDescriptor::Keyboard(
        // Should descriptor be something sensible?
        KeyboardDeviceDescriptor { keys: vec![] },
    )
}

fn into_input_event(keyboard_event: KeyboardEvent, event_time: Time) -> input_device::InputEvent {
    input_device::InputEvent {
        device_event: input_device::InputDeviceEvent::Keyboard(keyboard_event),
        device_descriptor: empty_keyboard_device_descriptor(),
        event_time,
        handled: input_device::Handled::No,
    }
}

#[async_trait(?Send)]
impl UnhandledInputHandler for DisplayOwnershipHandler {
    async fn handle_unhandled_input_event(
        self: std::rc::Rc<Self>,
        event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent> {
        let mut ret = vec![];
        let mut event_time = event.event_time;
        let is_display_unowned = self.is_display_unowned();
        if self.clone().is_ownership_modified() {
            // When the ownership is modified, float a set of cancel or sync
            // events to scoop up stale keyboard state, treating it the same
            // as loss of focus.
            let event_type = match is_display_unowned {
                true => KeyEventType::Cancel,
                false => KeyEventType::Sync,
            };
            let keys = self.key_state.borrow().get_set();
            // TODO(fxbug.dev/70249): This should happen as soon as display
            // ownership has changed.
            for key in keys.into_iter() {
                let key_event = KeyboardEvent::new(key, event_type);
                ret.push(into_input_event(key_event, event_time));
                event_time = event_time + Duration::from_nanos(1);
            }
        }
        match event.device_event {
            input_device::InputDeviceEvent::Keyboard(ref event) => {
                self.key_state.borrow_mut().update(event.get_event_type(), event.get_key());
            }
            _ => {}
        }
        // Mark this event as handled if we are informed that the display is not owned.
        // This should cause all subsequent handlers in the input pipeline
        // to see but ignore the event.
        //
        // TODO(fxbug.dev/70249): We probably also need to float a new "reset"
        // input event to clear any stateful handlers.
        ret.push(
            input_device::InputEvent::from(event)
                .into_with_event_time(event_time)
                .into_handled_if(is_display_unowned),
        );
        self.update_previous_ownership();
        ret
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::input_device::InputEvent;
    use crate::input_handler::InputHandler;
    use crate::testing_utilities::{create_fake_input_event, create_input_event};
    use fidl_fuchsia_input::Key;
    use fuchsia_zircon::{EventPair, Peered};
    use futures::channel::mpsc;
    use futures::StreamExt;
    use pretty_assertions::assert_eq;

    #[fuchsia::test]
    async fn display_ownership_change() {
        let (test_event, handler_event) = EventPair::create().unwrap();
        let (sender, mut receiver) = mpsc::unbounded::<()>();

        // Signal needs to be initialized before the handlers attempts to read it.
        // This is normally always the case in production.
        // Else, the `new_for_test` below will panic with a TIMEOUT error.
        test_event.signal_peer(*DISPLAY_OWNED, *DISPLAY_UNOWNED).unwrap();

        let fake_time = Time::from_nanos(42);
        let handler = DisplayOwnershipHandler::new_for_test(handler_event, sender);
        // Receiver is used to guarantee that the handler coroutine has advanced
        // as expected, here and below. This signal is only used in testing.
        receiver.next().await;

        // Go two full circles of signaling.

        // 1
        test_event.signal_peer(*DISPLAY_UNOWNED, *DISPLAY_OWNED).unwrap();
        receiver.next().await;
        let result = handler.clone().handle_input_event(create_fake_input_event(fake_time)).await;
        assert_eq!(result, vec![create_fake_input_event(fake_time)]);

        // 2
        test_event.signal_peer(*DISPLAY_OWNED, *DISPLAY_UNOWNED).unwrap();
        receiver.next().await;
        let result = handler.clone().handle_input_event(create_fake_input_event(fake_time)).await;
        assert_eq!(result, vec![create_fake_input_event(fake_time).into_handled()]);

        // 3
        test_event.signal_peer(*DISPLAY_UNOWNED, *DISPLAY_OWNED).unwrap();
        receiver.next().await;
        let result = handler.clone().handle_input_event(create_fake_input_event(fake_time)).await;
        assert_eq!(result, vec![create_fake_input_event(fake_time)]);

        // 4
        test_event.signal_peer(*DISPLAY_OWNED, *DISPLAY_UNOWNED).unwrap();
        receiver.next().await;
        let result = handler.clone().handle_input_event(create_fake_input_event(fake_time)).await;
        assert_eq!(result, vec![create_fake_input_event(fake_time).into_handled()]);
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
        let (sender, mut receiver) = mpsc::unbounded::<()>();
        test_event.signal_peer(*DISPLAY_UNOWNED, *DISPLAY_OWNED).unwrap();

        let handler = DisplayOwnershipHandler::new_for_test(handler_event, sender);
        receiver.next().await;

        let mut actual = vec![];

        actual.append(
            &mut handler
                .clone()
                .handle_input_event(new_keyboard_input_event(Key::A, KeyEventType::Pressed))
                .await,
        );
        actual.append(
            &mut handler
                .clone()
                .handle_input_event(new_keyboard_input_event(Key::A, KeyEventType::Released))
                .await,
        );
        assert_eq!(
            actual,
            vec![
                new_keyboard_input_event(Key::A, KeyEventType::Pressed),
                new_keyboard_input_event(Key::A, KeyEventType::Released),
            ]
        );
    }

    #[fuchsia::test]
    async fn state_handling_with_lost_display() {
        let (test_event, handler_event) = EventPair::create().unwrap();
        let (sender, mut receiver) = mpsc::unbounded::<()>();
        test_event.signal_peer(*DISPLAY_UNOWNED, *DISPLAY_OWNED).unwrap();

        let handler = DisplayOwnershipHandler::new_for_test(handler_event, sender);
        receiver.next().await;
        let mut actual = vec![];

        actual.append(
            &mut handler
                .clone()
                .handle_input_event(new_keyboard_input_event(Key::A, KeyEventType::Pressed))
                .await,
        );
        actual.append(
            &mut handler
                .clone()
                .handle_input_event(new_keyboard_input_event(Key::B, KeyEventType::Pressed))
                .await,
        );

        // We lose the display
        test_event.signal_peer(*DISPLAY_OWNED, *DISPLAY_UNOWNED).unwrap();
        receiver.next().await;

        actual.append(
            &mut handler
                .clone()
                .handle_input_event(new_keyboard_input_event(Key::A, KeyEventType::Released))
                .await,
        );
        actual.append(
            &mut handler
                .clone()
                .handle_input_event(new_keyboard_input_event(Key::B, KeyEventType::Released))
                .await,
        );

        // We regain the display
        test_event.signal_peer(*DISPLAY_UNOWNED, *DISPLAY_OWNED).unwrap();
        receiver.next().await;

        actual.append(
            &mut handler
                .clone()
                .handle_input_event(new_keyboard_input_event(Key::A, KeyEventType::Pressed))
                .await,
        );
        actual.append(
            &mut handler
                .clone()
                .handle_input_event(new_keyboard_input_event(Key::A, KeyEventType::Released))
                .await,
        );

        assert_eq!(
            actual,
            vec![
                // Keys active before switch.
                new_keyboard_input_event(Key::A, KeyEventType::Pressed),
                new_keyboard_input_event(Key::B, KeyEventType::Pressed),
                // Keys canceled after the switch.
                new_keyboard_input_event(Key::A, KeyEventType::Cancel)
                    .into_with_device_descriptor(empty_keyboard_device_descriptor()),
                new_keyboard_input_event(Key::B, KeyEventType::Cancel)
                    .into_with_event_time(Time::from_nanos(43))
                    .into_with_device_descriptor(empty_keyboard_device_descriptor()),
                // The key presses that should go to virtcon are marked as handled
                // so that the input pipeline stages ignore them.
                new_keyboard_input_event(Key::A, KeyEventType::Released)
                    .into_with_event_time(Time::from_nanos(44))
                    .into_handled(),
                new_keyboard_input_event(Key::B, KeyEventType::Released).into_handled(),
                // Press and release when the display is regained.
                new_keyboard_input_event(Key::A, KeyEventType::Pressed),
                new_keyboard_input_event(Key::A, KeyEventType::Released),
            ]
        );
    }
}
