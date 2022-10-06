// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::input_device::{Handled, InputDeviceEvent, InputEvent, UnhandledInputEvent};
use crate::input_handler::UnhandledInputHandler;
use async_trait::async_trait;
use fidl_fuchsia_ui_input3::{KeyMeaning, Modifiers, NonPrintableKey};
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
impl UnhandledInputHandler for ModifierHandler {
    async fn handle_unhandled_input_event(
        self: Rc<Self>,
        unhandled_input_event: UnhandledInputEvent,
    ) -> Vec<InputEvent> {
        match unhandled_input_event {
            UnhandledInputEvent {
                device_event: InputDeviceEvent::Keyboard(mut event),
                device_descriptor,
                event_time,
                trace_id: _,
            } => {
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
                    trace_id: None,
                }]
            }
            // Pass other events through.
            _ => vec![InputEvent::from(unhandled_input_event)],
        }
    }
}

impl ModifierHandler {
    /// Creates a new [ModifierHandler].
    pub fn new() -> Rc<Self> {
        Rc::new(Self {
            modifier_state: RefCell::new(ModifierState::new()),
            lock_state: RefCell::new(LockStateKeys::new()),
        })
    }
}

/// Tracks the state of the modifiers that are tied to the key meaning (as opposed to hardware
/// keys).
#[derive(Debug)]
pub struct ModifierMeaningHandler {
    /// The tracked state of the modifiers.
    modifier_state: RefCell<ModifierState>,
}

impl ModifierMeaningHandler {
    /// Creates a new [ModifierMeaningHandler].
    pub fn new() -> Rc<Self> {
        Rc::new(Self { modifier_state: RefCell::new(ModifierState::new()) })
    }
}

#[async_trait(?Send)]
impl UnhandledInputHandler for ModifierMeaningHandler {
    async fn handle_unhandled_input_event(
        self: Rc<Self>,
        unhandled_input_event: UnhandledInputEvent,
    ) -> Vec<InputEvent> {
        match unhandled_input_event {
            UnhandledInputEvent {
                device_event: InputDeviceEvent::Keyboard(mut event),
                device_descriptor,
                event_time,
                trace_id: _,
            } if event.get_key_meaning()
                == Some(KeyMeaning::NonPrintableKey(NonPrintableKey::AltGraph)) =>
            {
                // The "obvious" rewrite of this if and the match guard above is
                // unstable, so doing it this way.
                if let Some(key_meaning) = event.get_key_meaning() {
                    self.modifier_state
                        .borrow_mut()
                        .update_with_key_meaning(event.get_event_type(), key_meaning);
                    let new_modifier =
                        event.get_modifiers().unwrap_or(Modifiers::empty())
                            | self.modifier_state.borrow().get_state();
                    event = event.into_with_modifiers(Some(new_modifier));
                    fx_log_debug!("additinal modifiers and lock state applied: {:?}", &event);
                }
                vec![InputEvent {
                    device_event: InputDeviceEvent::Keyboard(event),
                    device_descriptor,
                    event_time,
                    handled: Handled::No,
                    trace_id: None,
                }]
            }
            // Pass other events through.
            _ => vec![InputEvent::from(unhandled_input_event)],
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::input_device::{InputDeviceDescriptor, InputDeviceEvent, InputEvent};
    use crate::keyboard_binding::KeyboardEvent;
    use fidl_fuchsia_input::Key;
    use fidl_fuchsia_ui_input3::{KeyEventType, LockState, Modifiers};
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use pretty_assertions::assert_eq;

    fn get_unhandled_input_event(event: KeyboardEvent) -> UnhandledInputEvent {
        UnhandledInputEvent {
            device_event: InputDeviceEvent::Keyboard(event),
            event_time: zx::Time::from_nanos(42),
            device_descriptor: InputDeviceDescriptor::Fake,
            trace_id: None,
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_decoration() {
        let handler = ModifierHandler::new();
        let input_event =
            get_unhandled_input_event(KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed));
        let result = handler.handle_unhandled_input_event(input_event.clone()).await;

        // This handler decorates, but does not handle the key. Hence,
        // the key remains unhandled.
        let expected = InputEvent::from(get_unhandled_input_event(
            KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed)
                .into_with_modifiers(Some(Modifiers::CAPS_LOCK))
                .into_with_lock_state(Some(LockState::CAPS_LOCK)),
        ));
        assert_eq!(vec![expected], result);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_key_meaning_decoration() {
        let handler = ModifierMeaningHandler::new();
        {
            let input_event = get_unhandled_input_event(
                KeyboardEvent::new(Key::RightAlt, KeyEventType::Pressed)
                    .into_with_key_meaning(Some(KeyMeaning::NonPrintableKey(
                        NonPrintableKey::AltGraph,
                    )))
                    .into_with_modifiers(Some(Modifiers::CAPS_LOCK)),
            );
            let result = handler.clone().handle_unhandled_input_event(input_event.clone()).await;
            let expected = InputEvent::from(get_unhandled_input_event(
                KeyboardEvent::new(Key::RightAlt, KeyEventType::Pressed)
                    .into_with_key_meaning(Some(KeyMeaning::NonPrintableKey(
                        NonPrintableKey::AltGraph,
                    )))
                    .into_with_modifiers(Some(Modifiers::ALT_GRAPH | Modifiers::CAPS_LOCK)),
            ));
            assert_eq!(vec![expected], result);
        }
        {
            let input_event = get_unhandled_input_event(
                KeyboardEvent::new(Key::RightAlt, KeyEventType::Released)
                    .into_with_key_meaning(Some(KeyMeaning::NonPrintableKey(
                        NonPrintableKey::AltGraph,
                    )))
                    .into_with_modifiers(Some(Modifiers::CAPS_LOCK)),
            );
            let handler = handler.clone();
            let result = handler.handle_unhandled_input_event(input_event.clone()).await;
            let expected = InputEvent::from(get_unhandled_input_event(
                KeyboardEvent::new(Key::RightAlt, KeyEventType::Released)
                    .into_with_key_meaning(Some(KeyMeaning::NonPrintableKey(
                        NonPrintableKey::AltGraph,
                    )))
                    .into_with_modifiers(Some(Modifiers::CAPS_LOCK)),
            ));
            assert_eq!(vec![expected], result);
        }
    }

    // CapsLock  """"""\______/""""""""""\_______/"""
    // Modifiers ------CCCCCCCC----------CCCCCCCCC---
    // LockState ------CCCCCCCCCCCCCCCCCCCCCCCCCCC---
    //
    // C == CapsLock
    #[fasync::run_singlethreaded(test)]
    async fn test_modifier_press_lock_release() {
        let input_events = vec![
            get_unhandled_input_event(KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed)),
            get_unhandled_input_event(KeyboardEvent::new(Key::CapsLock, KeyEventType::Released)),
            get_unhandled_input_event(KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed)),
            get_unhandled_input_event(KeyboardEvent::new(Key::CapsLock, KeyEventType::Released)),
        ];

        let handler = ModifierHandler::new();
        let clone_handler = move || handler.clone();
        let result = futures::future::join_all(
            input_events
                .into_iter()
                .map(|e| async { clone_handler().handle_unhandled_input_event(e).await }),
        )
        .await
        .into_iter()
        .flatten()
        .collect::<Vec<InputEvent>>();

        let expected = IntoIterator::into_iter([
            get_unhandled_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed)
                    .into_with_modifiers(Some(Modifiers::CAPS_LOCK))
                    .into_with_lock_state(Some(LockState::CAPS_LOCK)),
            ),
            get_unhandled_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Released)
                    .into_with_modifiers(Some(Modifiers::from_bits_allow_unknown(0)))
                    .into_with_lock_state(Some(LockState::CAPS_LOCK)),
            ),
            get_unhandled_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed)
                    .into_with_modifiers(Some(Modifiers::CAPS_LOCK))
                    .into_with_lock_state(Some(LockState::CAPS_LOCK)),
            ),
            get_unhandled_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Released)
                    .into_with_modifiers(Some(Modifiers::from_bits_allow_unknown(0)))
                    .into_with_lock_state(Some(LockState::from_bits_allow_unknown(0))),
            ),
        ])
        .map(InputEvent::from)
        .collect::<Vec<_>>();

        assert_eq!(expected, result);
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
            get_unhandled_input_event(KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed)),
            get_unhandled_input_event(KeyboardEvent::new(Key::CapsLock, KeyEventType::Released)),
            get_unhandled_input_event(KeyboardEvent::new(Key::A, KeyEventType::Pressed)),
            get_unhandled_input_event(KeyboardEvent::new(Key::A, KeyEventType::Released)),
        ];

        let handler = ModifierHandler::new();
        let clone_handler = move || handler.clone();
        let result = futures::future::join_all(
            input_events
                .into_iter()
                .map(|e| async { clone_handler().handle_unhandled_input_event(e).await }),
        )
        .await
        .into_iter()
        .flatten()
        .collect::<Vec<InputEvent>>();

        let expected = IntoIterator::into_iter([
            get_unhandled_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Pressed)
                    .into_with_modifiers(Some(Modifiers::CAPS_LOCK))
                    .into_with_lock_state(Some(LockState::CAPS_LOCK)),
            ),
            get_unhandled_input_event(
                KeyboardEvent::new(Key::CapsLock, KeyEventType::Released)
                    .into_with_modifiers(Some(Modifiers::from_bits_allow_unknown(0)))
                    .into_with_lock_state(Some(LockState::CAPS_LOCK)),
            ),
            get_unhandled_input_event(
                KeyboardEvent::new(Key::A, KeyEventType::Pressed)
                    .into_with_modifiers(Some(Modifiers::from_bits_allow_unknown(0)))
                    .into_with_lock_state(Some(LockState::CAPS_LOCK)),
            ),
            get_unhandled_input_event(
                KeyboardEvent::new(Key::A, KeyEventType::Released)
                    .into_with_modifiers(Some(Modifiers::from_bits_allow_unknown(0)))
                    .into_with_lock_state(Some(LockState::CAPS_LOCK)),
            ),
        ])
        .map(InputEvent::from)
        .collect::<Vec<_>>();
        assert_eq!(expected, result);
    }
}
