// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::input_device::{Handled, InputDeviceEvent, InputEvent};
use crate::input_handler::InputHandler;
use async_trait::async_trait;
use fuchsia_syslog::fx_log_debug;
use keymaps::{LockStateKeys, ModifierState};
use std::cell::RefCell;
use std::rc::Rc;

/// Tracks modifier state and decorates passing events with the modifiers.
///
/// This handler should be installed as early as possible in the input pipeline,
/// to ensure that all later stages have the modifiers and lock states available.
/// This holds even for non-keyboard handlers, to allow handling `Ctrl+Click`
/// events, for example.
///
/// One possible exception to this rule would be a hardware key rewriting handler for
/// limited keyboards.
#[derive(Debug)]
pub struct ModifierHandler {
    /// The tracked state of the modifiers.
    modifier_state: RefCell<ModifierState>,

    /// The tracked lock state.
    lock_state: RefCell<LockStateKeys>,
}

#[async_trait(?Send)]
impl InputHandler for ModifierHandler {
    async fn handle_input_event(self: Rc<Self>, input_event: InputEvent) -> Vec<InputEvent> {
        match input_event {
            InputEvent {
                device_event: InputDeviceEvent::Keyboard(mut event),
                device_descriptor,
                event_time,
                handled,
            } if handled == Handled::No => {
                self.modifier_state.borrow_mut().update(event.get_event_type(), event.get_key());
                self.lock_state.borrow_mut().update(event.get_event_type(), event.get_key());
                event = event
                    .into_with_lock_state(Some(self.lock_state.borrow().get_state()))
                    .into_with_modifiers(Some(self.modifier_state.borrow().get_state()));
                fx_log_debug!("modifiers and lock state applied: {:?}", &event);
                vec![InputEvent {
                    device_event: InputDeviceEvent::Keyboard(event),
                    device_descriptor,
                    event_time,
                    handled: Handled::No,
                }]
            }
            // Pass other events through:
            //   - Handled::Yes events are propagated as an input pipeline
            //     invariant.
            //   - All other events are propagated.
            _ => vec![input_event],
        }
    }
}

impl ModifierHandler {
    /// Creates a new [ModifierHandler].
    pub fn new() -> Rc<Self> {
        Rc::new(ModifierHandler {
            modifier_state: RefCell::new(ModifierState::new()),
            lock_state: RefCell::new(LockStateKeys::new()),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::input_device::{EventTime, InputDeviceDescriptor, InputDeviceEvent};
    use crate::keyboard_binding::KeyboardEvent;
    use crate::testing_utilities::diff_input_events;
    use fidl_fuchsia_input::Key;
    use fidl_fuchsia_ui_input3::{KeyEventType, LockState, Modifiers};
    use fuchsia_async as fasync;

    fn get_fake_input_event(event: KeyboardEvent, handled: Handled) -> InputEvent {
        InputEvent {
            device_event: InputDeviceEvent::Keyboard(event),
            event_time: 42 as EventTime,
            device_descriptor: InputDeviceDescriptor::Fake,
            handled,
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_decoration() {
        let handler = ModifierHandler::new();
        let input_event = get_fake_input_event(
            KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed),
            Handled::No,
        );
        let result = handler.handle_input_event(input_event.clone()).await;

        let expected = get_fake_input_event(
            KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed)
                .into_with_modifiers(Some(Modifiers::CapsLock))
                .into_with_lock_state(Some(LockState::CapsLock)),
            // This handler decorates, but does not handle the key. Hence,
            // the key remains unhandled.
            Handled::No,
        );
        assert_eq!(vec![expected], result);
    }

    // CapsLock  """"""\______/""""""""""\_______/"""
    // Modifiers ------CCCCCCCC----------CCCCCCCCC---
    // LockState ------CCCCCCCCCCCCCCCCCCCCCCCCCCC---
    //
    // C == CapsLock
    #[fasync::run_singlethreaded(test)]
    async fn test_modifier_press_lock_release() {
        let input_events = vec![
            get_fake_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed),
                Handled::No,
            ),
            get_fake_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Released),
                Handled::No,
            ),
            get_fake_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed),
                Handled::No,
            ),
            get_fake_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Released),
                Handled::No,
            ),
        ];

        let handler = ModifierHandler::new();
        let clone_handler = move || handler.clone();
        let result = futures::future::join_all(
            input_events.into_iter().map(|e| async { clone_handler().handle_input_event(e).await }),
        )
        .await
        .into_iter()
        .flatten()
        .collect::<Vec<InputEvent>>();

        let expected = vec![
            get_fake_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed)
                    .into_with_modifiers(Some(Modifiers::CapsLock))
                    .into_with_lock_state(Some(LockState::CapsLock)),
                Handled::No,
            ),
            get_fake_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Released)
                    .into_with_modifiers(Some(Modifiers::from_bits_allow_unknown(0)))
                    .into_with_lock_state(Some(LockState::CapsLock)),
                Handled::No,
            ),
            get_fake_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed)
                    .into_with_modifiers(Some(Modifiers::CapsLock))
                    .into_with_lock_state(Some(LockState::CapsLock)),
                Handled::No,
            ),
            get_fake_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Released)
                    .into_with_modifiers(Some(Modifiers::from_bits_allow_unknown(0)))
                    .into_with_lock_state(Some(LockState::from_bits_allow_unknown(0))),
                Handled::No,
            ),
        ];
        assert_eq!(expected, result, "diff:\n{}", diff_input_events(&expected, &result));
    }

    // CapsLock  """"""\______/"""""""""""""""""""
    // A         """""""""""""""""""\________/""""
    // Modifiers ------CCCCCCCC-------------------
    // LockState ------CCCCCCCCCCCCCCCCCCCCCCCCCCC
    //
    // C == CapsLock
    #[fasync::run_singlethreaded(test)]
    async fn repeated_modifier_key() {
        let input_events = vec![
            get_fake_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed),
                Handled::No,
            ),
            get_fake_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Released),
                Handled::No,
            ),
            get_fake_input_event(KeyboardEvent::new(Key::A, KeyEventType::Pressed), Handled::No),
            get_fake_input_event(KeyboardEvent::new(Key::A, KeyEventType::Released), Handled::No),
        ];

        let handler = ModifierHandler::new();
        let clone_handler = move || handler.clone();
        let result = futures::future::join_all(
            input_events.into_iter().map(|e| async { clone_handler().handle_input_event(e).await }),
        )
        .await
        .into_iter()
        .flatten()
        .collect::<Vec<InputEvent>>();

        let expected = vec![
            get_fake_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed)
                    .into_with_modifiers(Some(Modifiers::CapsLock))
                    .into_with_lock_state(Some(LockState::CapsLock)),
                Handled::No,
            ),
            get_fake_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Released)
                    .into_with_modifiers(Some(Modifiers::from_bits_allow_unknown(0)))
                    .into_with_lock_state(Some(LockState::CapsLock)),
                Handled::No,
            ),
            get_fake_input_event(
                KeyboardEvent::new(Key::A, KeyEventType::Pressed)
                    .into_with_modifiers(Some(Modifiers::from_bits_allow_unknown(0)))
                    .into_with_lock_state(Some(LockState::CapsLock)),
                Handled::No,
            ),
            get_fake_input_event(
                KeyboardEvent::new(Key::A, KeyEventType::Released)
                    .into_with_modifiers(Some(Modifiers::from_bits_allow_unknown(0)))
                    .into_with_lock_state(Some(LockState::CapsLock)),
                Handled::No,
            ),
        ];
        assert_eq!(expected, result, "\n\ndiff:\n{}", diff_input_events(&expected, &result));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_decoration_handled() {
        let handler = ModifierHandler::new();
        let input_event = get_fake_input_event(
            KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed),
            Handled::Yes,
        );
        let result = handler.handle_input_event(input_event.clone()).await;

        let expected = input_event.clone();
        assert_eq!(vec![expected], result);
    }
}
