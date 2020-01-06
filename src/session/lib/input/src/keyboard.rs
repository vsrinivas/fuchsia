// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_device::InputDeviceBinding,
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport},
    fidl_fuchsia_ui_input2::{Key, KeyEventPhase, Modifiers},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::{
        channel::mpsc::{Receiver, Sender},
        SinkExt, StreamExt,
    },
    maplit::hashmap,
    std::collections::HashMap,
};

/// A [`KeyboardInputMessage`] represents an input event from a keyboard device.
///
/// The input message contains information about which keys are pressed, and which
/// keys were released.
///
/// Clients can expect the following sequence of events for a given key:
///
/// 1. [`KeyEventPhase::Pressed`]
/// 2. [`KeyEventPhase::Released`]
///
/// No duplicate [`KeyEventPhase::Pressed`] events will be sent for keys, even if the
/// key is present in a subsequent [`InputReport`]. Clients can assume that
/// a key is pressed for all received input messages until the key is present in
/// the [`KeyEventPhase::Released`] entry of [`keys`].
pub struct KeyboardInputMessage {
    /// The keys associated with this input message, sorted by their `KeyEventPhase`
    /// (e.g., whether they are pressed or released).
    pub keys: HashMap<KeyEventPhase, Vec<Key>>,
}

/// A [`KeyboardDescriptor`] contains information about a specific keyboard device.
#[derive(Clone)]
pub struct KeyboardDescriptor {
    /// All the keys available on the keyboard device.
    pub keys: Vec<Key>,
}

/// A [`KeyboardBinding`] represents a connection to a keyboard input device.
///
/// The [`KeyboardBinding`] parses and exposes keyboard descriptor properties (e.g., the available
/// keyboard keys) for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to clients via the stream available at
/// [`KeyboardBinding::input_message_stream()`].
///
/// # Example
/// ```
/// let mut keyboard_device: KeyboardBinding = input_device::InputDeviceBinding::new().await?;
///
/// while let Some(report) = keyboard_device.input_message_stream().next().await {}
/// ```
pub struct KeyboardBinding {
    /// The channel to stream InputReports to
    message_sender: Sender<input_device::InputMessage>,

    /// The receiving end of the input report channel. Clients use this indirectly via
    /// [`input_messages()`].
    message_receiver: Receiver<input_device::InputMessage>,

    /// Holds information about this device.
    descriptor: KeyboardDescriptor,
}

#[async_trait]
impl input_device::InputDeviceBinding for KeyboardBinding {
    fn input_message_sender(&self) -> Sender<input_device::InputMessage> {
        self.message_sender.clone()
    }

    fn input_message_stream(&mut self) -> &mut Receiver<input_device::InputMessage> {
        return &mut self.message_receiver;
    }

    fn get_descriptor(&self) -> input_device::InputDescriptor {
        input_device::InputDescriptor::Keyboard(self.descriptor.clone())
    }

    fn process_reports(
        report: InputReport,
        previous_report: Option<InputReport>,
        _device_descriptor: &mut input_device::InputDescriptor,
        input_message_sender: &mut Sender<input_device::InputMessage>,
    ) -> Option<InputReport> {
        let new_keys = match KeyboardBinding::parse_pressed_keys(&report) {
            Some(keys) => keys,
            None => {
                // It's OK for the report to contain an empty vector of keys, but it's not OK for
                // the report to not have the appropriate fields set.
                //
                // In this case the report is treated as malformed, and the previous report is not
                // updated.
                fx_log_err!("Failed to parse keyboard keys: {:?}", report);
                return previous_report;
            }
        };

        // For the previous keys it's OK to not be able to parse the keys, since an empty vector is
        // a sensible default (this is what happens when there is no previous report).
        let previous_keys: Vec<Key> = previous_report
            .as_ref()
            .and_then(|unwrapped_report| KeyboardBinding::parse_pressed_keys(&unwrapped_report))
            .unwrap_or_default();

        KeyboardBinding::send_key_events(&new_keys, &previous_keys, input_message_sender.clone());

        Some(report)
    }

    async fn any_input_device() -> Result<InputDeviceProxy, Error> {
        let mut devices = Self::all_devices().await?;
        devices.pop().ok_or(format_err!("Couldn't find a default keyboard."))
    }

    async fn all_devices() -> Result<Vec<InputDeviceProxy>, Error> {
        input_device::all_devices(input_device::InputDeviceType::Keyboard).await
    }

    async fn bind_device(device: &InputDeviceProxy) -> Result<Self, Error> {
        match device.get_descriptor().await?.keyboard {
            Some(fidl_fuchsia_input_report::KeyboardDescriptor {
                input: Some(fidl_fuchsia_input_report::KeyboardInputDescriptor { keys }),
            }) => {
                let (message_sender, message_receiver) =
                    futures::channel::mpsc::channel(input_device::INPUT_MESSAGE_BUFFER_SIZE);
                Ok(KeyboardBinding {
                    message_sender,
                    message_receiver,
                    descriptor: KeyboardDescriptor { keys: keys.unwrap_or_default() },
                })
            }
            descriptor => {
                Err(format_err!("Keyboard Descriptor failed to parse: \n {:?}", descriptor))
            }
        }
    }
}

/// Returns a vector of [`KeyboardBindings`] for all currently connected keyboards.
///
/// # Errors
/// If there was an error binding to any keyboard.
async fn all_keyboard_bindings() -> Result<Vec<KeyboardBinding>, Error> {
    let device_proxies = input_device::all_devices(input_device::InputDeviceType::Keyboard).await?;
    let mut device_bindings: Vec<KeyboardBinding> = vec![];

    for device_proxy in device_proxies {
        let device_binding: KeyboardBinding =
            input_device::InputDeviceBinding::new(device_proxy).await?;
        device_bindings.push(device_binding);
    }

    Ok(device_bindings)
}

/// Returns a stream of InputMessages from all keyboard devices.
///
/// # Errors
/// If there was an error binding to any keyboard.
pub async fn all_keyboard_messages() -> Result<Receiver<input_device::InputMessage>, Error> {
    let bindings = all_keyboard_bindings().await?;
    let (message_sender, message_receiver) =
        futures::channel::mpsc::channel(input_device::INPUT_MESSAGE_BUFFER_SIZE);

    for mut keyboard in bindings {
        let mut sender = message_sender.clone();
        fasync::spawn(async move {
            while let Some(report) = keyboard.input_message_stream().next().await {
                let _ = sender.try_send(report);
            }
        });
    }

    Ok(message_receiver)
}

impl KeyboardBinding {
    /// Converts a vector of keyboard keys to the appropriate [`Modifiers`] bitflags.
    ///
    /// For example, if `keys` contains `Key::LeftAlt`, the bitflags will contain
    /// the corresponding flags for `LeftAlt`.
    ///
    /// # Parameters
    /// - `keys`: The keys to check for modifiers.
    ///
    /// # Returns
    /// Returns `None` if there are no modifier keys present.
    pub fn to_modifiers(keys: &Vec<&Key>) -> Option<Modifiers> {
        let mut modifiers = Modifiers::empty();
        for key in keys {
            let modifier = match key {
                Key::LeftAlt => Some(Modifiers::Alt | Modifiers::LeftAlt),
                Key::RightAlt => Some(Modifiers::Alt | Modifiers::RightAlt),
                Key::LeftShift => Some(Modifiers::Shift | Modifiers::LeftShift),
                Key::RightShift => Some(Modifiers::Shift | Modifiers::RightShift),
                Key::LeftCtrl => Some(Modifiers::Control | Modifiers::LeftControl),
                Key::RightCtrl => Some(Modifiers::Control | Modifiers::RightControl),
                Key::LeftMeta => Some(Modifiers::Meta | Modifiers::LeftMeta),
                Key::RightMeta => Some(Modifiers::Meta | Modifiers::RightMeta),
                Key::CapsLock => Some(Modifiers::CapsLock),
                Key::NumLock => Some(Modifiers::NumLock),
                Key::ScrollLock => Some(Modifiers::ScrollLock),
                _ => None,
            };
            if let Some(modifier) = modifier {
                modifiers.insert(modifier);
            };
        }
        if modifiers.is_empty() {
            return None;
        }
        Some(modifiers)
    }

    /// Parses the currently pressed keys from an input report.
    ///
    /// # Parameters
    /// - `input_report`: The input report to parse the keyboard keys from.
    ///
    /// # Returns
    /// Returns `None` if any of the required input report fields are `None`. If all the
    /// required report fields are present, but there are no pressed keys, an empty vector
    /// is returned.
    fn parse_pressed_keys(input_report: &InputReport) -> Option<Vec<Key>> {
        input_report
            .keyboard
            .as_ref()
            .and_then(|unwrapped_keyboard| unwrapped_keyboard.pressed_keys.as_ref())
            .and_then(|unwrapped_keys| Some(unwrapped_keys.iter().cloned().collect()))
    }

    /// Sends key events to clients based on the new and previously pressed keys.
    ///
    /// # Parameters
    /// - `new_keys`: The keys which are currently pressed, as reported by the bound device.
    /// - `previous_keys`: The keys which were pressed in the previous input report.
    fn send_key_events(
        new_keys: &Vec<Key>,
        previous_keys: &Vec<Key>,
        mut input_message_sender: Sender<input_device::InputMessage>,
    ) {
        // Filter out the keys which were present in the previous keyboard report to avoid sending
        // multiple `KeyEventPhase::Pressed` events for a key.
        let pressed_keys: Vec<Key> =
            new_keys.iter().cloned().filter(|key| !previous_keys.contains(key)).collect();

        // Any key which is not present in the new keys, but was present in the previous report
        // is considered to be released.
        let released_keys: Vec<Key> =
            previous_keys.iter().cloned().filter(|key| !new_keys.contains(key)).collect();

        let keys_to_send = hashmap! {
            KeyEventPhase::Pressed => pressed_keys,
            KeyEventPhase::Released => released_keys,
        };

        fasync::spawn(async move {
            match input_message_sender
                .send(input_device::InputMessage::Keyboard(KeyboardInputMessage {
                    keys: keys_to_send,
                }))
                .await
            {
                Err(error) => {
                    fx_log_err!("Failed to send keyboard key events: {:?}", error);
                }
                _ => (),
            }
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn create_input_report(pressed_keys: Vec<Key>) -> InputReport {
        InputReport {
            event_time: None,
            keyboard: Some(fidl_fuchsia_input_report::KeyboardInputReport {
                pressed_keys: Some(pressed_keys),
            }),
            mouse: None,
            touch: None,
            sensor: None,
            trace_id: None,
        }
    }

    /// Tests that a key that is present in the new report, but was not present in the previous report
    /// is propagated as pressed.
    #[fasync::run_singlethreaded(test)]
    async fn pressed_key() {
        let previous_report = None;
        let report = create_input_report(vec![Key::A]);

        let mut descriptor =
            input_device::InputDescriptor::Keyboard(KeyboardDescriptor { keys: vec![] });

        let (message_sender, mut message_receiver) = futures::channel::mpsc::channel(1);
        let _ = KeyboardBinding::process_reports(
            report,
            previous_report,
            &mut descriptor,
            &mut message_sender.clone(),
        );

        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Keyboard(keyboard_message) => {
                    assert_eq!(keyboard_message.keys[&KeyEventPhase::Pressed].len(), 1);
                    assert_eq!(
                        keyboard_message.keys[&KeyEventPhase::Pressed].first(),
                        Some(&Key::A)
                    );

                    assert_eq!(keyboard_message.keys[&KeyEventPhase::Released].len(), 0);
                }
                _ => assert!(false),
            }
        } else {
            assert!(false);
        }
    }

    /// Tests that a key that is not present in the new report, but was present in the previous report
    /// is propagated as released.
    #[fasync::run_singlethreaded(test)]
    async fn released_key() {
        let previous_report = create_input_report(vec![Key::A]);
        let report = create_input_report(vec![]);

        let mut descriptor =
            input_device::InputDescriptor::Keyboard(KeyboardDescriptor { keys: vec![] });

        let (message_sender, mut message_receiver) = futures::channel::mpsc::channel(1);
        let _ = KeyboardBinding::process_reports(
            report,
            Some(previous_report),
            &mut descriptor,
            &mut message_sender.clone(),
        );

        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Keyboard(keyboard_message) => {
                    assert_eq!(keyboard_message.keys[&KeyEventPhase::Released].len(), 1);
                    assert_eq!(
                        keyboard_message.keys[&KeyEventPhase::Released].first(),
                        Some(&Key::A)
                    );

                    assert_eq!(keyboard_message.keys[&KeyEventPhase::Pressed].len(), 0);
                }
                _ => assert!(false),
            }
        } else {
            assert!(false);
        }
    }

    /// Tests that a key that is present in multiple consecutive input reports is not propagated
    /// as a pressed event more than once.
    #[fasync::run_singlethreaded(test)]
    async fn multiple_pressed_event_filtering() {
        let previous_report = create_input_report(vec![Key::A]);
        let report = create_input_report(vec![Key::A]);

        let mut descriptor =
            input_device::InputDescriptor::Keyboard(KeyboardDescriptor { keys: vec![] });

        let (message_sender, mut message_receiver) = futures::channel::mpsc::channel(1);
        let _ = KeyboardBinding::process_reports(
            report,
            Some(previous_report),
            &mut descriptor,
            &mut message_sender.clone(),
        );

        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Keyboard(keyboard_message) => {
                    assert_eq!(keyboard_message.keys[&KeyEventPhase::Released].len(), 0);
                    assert_eq!(keyboard_message.keys[&KeyEventPhase::Pressed].len(), 0);
                }
                _ => assert!(false),
            }
        } else {
            assert!(false);
        }
    }

    /// Tests that both pressed and released keys are sent at once.
    #[fasync::run_singlethreaded(test)]
    async fn pressed_and_released_keys() {
        let previous_report = create_input_report(vec![Key::A]);
        let report = create_input_report(vec![Key::B]);

        let mut descriptor =
            input_device::InputDescriptor::Keyboard(KeyboardDescriptor { keys: vec![] });

        let (message_sender, mut message_receiver) = futures::channel::mpsc::channel(1);
        let _ = KeyboardBinding::process_reports(
            report,
            Some(previous_report),
            &mut descriptor,
            &mut message_sender.clone(),
        );

        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Keyboard(keyboard_message) => {
                    assert_eq!(keyboard_message.keys[&KeyEventPhase::Released].len(), 1);
                    assert_eq!(keyboard_message.keys[&KeyEventPhase::Pressed].len(), 1);

                    assert_eq!(
                        keyboard_message.keys[&KeyEventPhase::Released].first(),
                        Some(&Key::A)
                    );
                    assert_eq!(
                        keyboard_message.keys[&KeyEventPhase::Pressed].first(),
                        Some(&Key::B)
                    );
                }
                _ => assert!(false),
            }
        } else {
            assert!(false);
        }
    }

    /// Tests that modifier keys are propagated to the message receiver.
    #[fasync::run_singlethreaded(test)]
    async fn modifier_keys() {
        let previous_report = None;
        let report = create_input_report(vec![Key::LeftShift]);

        let mut descriptor =
            input_device::InputDescriptor::Keyboard(KeyboardDescriptor { keys: vec![] });

        let (message_sender, mut message_receiver) = futures::channel::mpsc::channel(1);
        let _ = KeyboardBinding::process_reports(
            report,
            previous_report,
            &mut descriptor,
            &mut message_sender.clone(),
        );

        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Keyboard(keyboard_message) => {
                    assert_eq!(keyboard_message.keys[&KeyEventPhase::Pressed].len(), 1);

                    assert_eq!(
                        keyboard_message.keys[&KeyEventPhase::Pressed].first(),
                        Some(&Key::LeftShift)
                    );
                }
                _ => assert!(false),
            }
        } else {
            assert!(false);
        }
    }
}
