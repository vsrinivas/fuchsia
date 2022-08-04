// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Recognizes Alt+Shift+i as the immersive mode shortcut, and passes
//! an event to downstream handlers indicating that immersive mode
//! should be toggled.
//!
//! Note: This handler deals only in `UnhandledInputEvent`s. When
//! instantiating this handler, clients should ensure that either
//! a) there are no handlers upstream of this handler that mark
//!    `KeyboardEvent`s as handled, OR
//! b) it is appropriate for the immersive mode shortcut to be
//!    ignored when the upstream handler(s) mark the corresponding
//!    `KeyboardEvent` as handled.

use {
    crate::{input_device, input_handler::UnhandledInputHandler, mouse_config},
    async_trait::async_trait,
    fidl_fuchsia_input::Key,
    fidl_fuchsia_ui_input3::{KeyEventType, Modifiers},
    std::{convert::From, rc::Rc},
};

// TODO(fxbug.dev/90290): Remove this handler when we have a proper cursor API.
pub struct ImmersiveModeShortcutHandler;

#[async_trait(?Send)]
impl UnhandledInputHandler for ImmersiveModeShortcutHandler {
    async fn handle_unhandled_input_event(
        self: Rc<Self>,
        unhandled_input_event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent> {
        match unhandled_input_event {
            input_device::UnhandledInputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(ref keyboard_device_event),
                device_descriptor: input_device::InputDeviceDescriptor::Keyboard(_),
                event_time,
                trace_id: _,
            } if keyboard_device_event.get_event_type() == KeyEventType::Pressed
                && keyboard_device_event.get_key() == Key::I
                && keyboard_device_event.get_unsided_modifiers()
                    == Modifiers::ALT | Modifiers::SHIFT =>
            {
                return vec![
                    input_device::InputEvent::from(unhandled_input_event).into_handled(),
                    input_device::InputEvent {
                        device_event: input_device::InputDeviceEvent::MouseConfig(
                            mouse_config::MouseConfigEvent::ToggleImmersiveMode,
                        ),
                        device_descriptor: input_device::InputDeviceDescriptor::MouseConfig,
                        event_time: event_time,
                        handled: input_device::Handled::No,
                        trace_id: None,
                    },
                ];
            }
            _ => vec![input_device::InputEvent::from(unhandled_input_event)],
        }
    }
}

impl ImmersiveModeShortcutHandler {
    /// Creates a new [`ImmersiveModeShortcutHandler`].
    ///
    /// Returns `Rc<Self>`
    pub fn new() -> Rc<Self> {
        Rc::new(Self {})
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::keyboard_binding, test_case::test_case};

    std::thread_local! {static NEXT_EVENT_TIME: std::cell::Cell<i64> = std::cell::Cell::new(0)}

    fn make_unhandled_input_event(
        keyboard_event: keyboard_binding::KeyboardEvent,
    ) -> input_device::UnhandledInputEvent {
        let event_time = NEXT_EVENT_TIME.with(|t| {
            let old = t.get();
            t.set(old + 1);
            old
        });
        input_device::UnhandledInputEvent {
            device_event: input_device::InputDeviceEvent::Keyboard(keyboard_event),
            device_descriptor: input_device::InputDeviceDescriptor::Keyboard(
                keyboard_binding::KeyboardDeviceDescriptor {
                    keys: vec![Key::I, Key::J],
                    ..Default::default()
                },
            ),
            event_time: fuchsia_zircon::Time::from_nanos(event_time),
            trace_id: None,
        }
    }

    #[test_case(
            (KeyEventType::Pressed, Key::I, Some(Modifiers::ALT | Modifiers::SHIFT))
                => input_device::Handled::Yes;
                "press-alt-shift-i")]
    #[test_case(
            (KeyEventType::Pressed, Key::J, Some(Modifiers::ALT | Modifiers::SHIFT))
                => input_device::Handled::No;
                "press-alt-shift-j")]
    #[test_case(
            (KeyEventType::Pressed, Key::I, Some(Modifiers::SHIFT)) => input_device::Handled::No;
                "press-shift-i")]
    #[test_case(
            (KeyEventType::Pressed, Key::I, None) => input_device::Handled::No;
                "press-i")]
    #[test_case(
            (KeyEventType::Pressed, Key::I,
             Some(Modifiers::CTRL | Modifiers::ALT | Modifiers::SHIFT))
                => input_device::Handled::No;
                "press-ctrl-alt-shift-i")]
    #[test_case(
            (KeyEventType::Released, Key::I, Some(Modifiers::ALT | Modifiers::SHIFT))
                => input_device::Handled::No;
                "release-alt-shift-i")]
    #[test_case(
            (KeyEventType::Pressed, Key::I,
             Some(Modifiers::ALT | Modifiers::SHIFT | Modifiers::LEFT_SHIFT))
                => input_device::Handled::Yes;
                "raw-press-alt-leftshift-i")]
    #[fuchsia::test(allow_stalls = false)]
    async fn propagates_keyboard_event_with_correct_handled_flag(
        (event_type, key, modifiers): (KeyEventType, Key, Option<Modifiers>),
    ) -> input_device::Handled {
        let handler = ImmersiveModeShortcutHandler::new();
        let input_event = make_unhandled_input_event(
            keyboard_binding::KeyboardEvent::new(key, event_type).into_with_modifiers(modifiers),
        );
        let output_events = handler.clone().handle_unhandled_input_event(input_event).await;
        let keyboard_events = output_events
            .iter()
            .filter(|event| match event.device_event {
                input_device::InputDeviceEvent::Keyboard(_) => true,
                _ => false,
            })
            .collect::<Vec<_>>();
        assert_eq!(
            keyboard_events.len(),
            1,
            "expected exactly one InputDeviceEvent::Keyboard, but got {:?}",
            output_events
        );
        keyboard_events[0].handled
    }

    #[test_case(
        (KeyEventType::Pressed, Key::I, Some(Modifiers::ALT | Modifiers::SHIFT))
            => true;
            "press-alt-shift-i")]
    #[test_case(
        (KeyEventType::Released, Key::I, Some(Modifiers::ALT | Modifiers::SHIFT))
            => false;
            "release-alt-shift-i")]
    #[fuchsia::test(allow_stalls = false)]
    async fn sends_toggle_event(
        (event_type, key, modifiers): (KeyEventType, Key, Option<Modifiers>),
    ) -> bool {
        let handler = ImmersiveModeShortcutHandler::new();
        let input_event = make_unhandled_input_event(
            keyboard_binding::KeyboardEvent::new(key, event_type).into_with_modifiers(modifiers),
        );
        let output_events = handler.clone().handle_unhandled_input_event(input_event).await;
        let config_events = output_events
            .iter()
            .filter(|event| match event.device_event {
                input_device::InputDeviceEvent::MouseConfig(_) => true,
                _ => false,
            })
            .collect::<Vec<_>>();
        assert!(
            config_events.len() <= 1,
            "expected <=1 InputDeviceEvent::MouseConfig, but got {:?}",
            output_events
        );
        config_events.len() > 0
    }
}
