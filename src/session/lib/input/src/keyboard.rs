// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device::{self, InputDeviceBinding},
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_input,
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport},
    fidl_fuchsia_ui_input2 as fidl_ui_input2,
    fidl_fuchsia_ui_input2::KeyEventPhase,
    fidl_fuchsia_ui_input3 as fidl_ui_input3,
    fidl_fuchsia_ui_input3::KeyEventType,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::{channel::mpsc::Sender, SinkExt},
    input_synthesis::usages::{is_modifier, is_modifier3},
    maplit::hashmap,
    std::collections::HashMap,
};

/// A [`KeyboardEvent`] represents an input event from a keyboard device.
///
/// The keyboard event contains information about which keys are pressed, and which
/// keys were released.
///
/// Clients can expect the following sequence of events for a given key:
///
/// 1. [`KeyEventType::Pressed`]
/// 2. [`KeyEventType::Released`]
///
/// No duplicate [`KeyEventType::Pressed`] events will be sent for keys, even if the
/// key is present in a subsequent [`InputReport`]. Clients can assume that
/// a key is pressed for all received input events until the key is present in
/// the [`KeyEventType::Released`] entry of [`keys`].
#[derive(Clone, Debug, PartialEq)]
pub struct KeyboardEvent {
    /// The keys associated with this input event, sorted by their [`KeyEventPhase`]
    /// (e.g., whether they are pressed or released).
    pub keys2: HashMap<KeyEventPhase, Vec<fidl_ui_input2::Key>>,

    /// The keys associated with this input event, sorted by their [`KeyEventType`]
    /// (e.g., whether they are pressed or released).
    pub keys3: HashMap<KeyEventType, Vec<fidl_fuchsia_input::Key>>,

    /// The [`fidl_ui_input2::Modifiers`] associated with the pressed keys.
    pub modifiers2: Option<fidl_ui_input2::Modifiers>,

    /// The [`fidl_ui_input3::Modifiers`] associated with the pressed keys.
    pub modifiers3: Option<fidl_ui_input3::Modifiers>,
}

impl KeyboardEvent {
    /// Returns the [`fidl_ui_input2::Key`]s of the specified `phase`.
    ///
    /// # Parameters
    /// `phase`: The phase of the keys to return.
    pub fn get_keys2(&self, phase: KeyEventPhase) -> Vec<fidl_ui_input2::Key> {
        let keys: Option<&Vec<fidl_ui_input2::Key>> = self.keys2.get(&phase);
        if keys.is_some() {
            return keys.unwrap().to_vec();
        }

        vec![]
    }

    /// Returns the [`fidl_fuchsia_input::Key`]s of the specified `event_type`.
    ///
    /// # Parameters
    /// `event_type`: The type of the keys to return.
    pub fn get_keys3(&self, event_type: KeyEventType) -> Vec<fidl_fuchsia_input::Key> {
        let keys: Option<&Vec<fidl_fuchsia_input::Key>> = self.keys3.get(&event_type);
        if keys.is_some() {
            return keys.unwrap().to_vec();
        }

        vec![]
    }
}

/// A [`KeyboardDeviceDescriptor`] contains information about a specific keyboard device.
#[derive(Clone, Debug, PartialEq)]
pub struct KeyboardDeviceDescriptor {
    /// All the [`fidl_ui_input2::Key`]s available on the keyboard device.
    pub keys2: Vec<fidl_ui_input2::Key>,

    /// All the [`fidl_fuchsia_input::Key`]s available on the keyboard device.
    pub keys3: Vec<fidl_fuchsia_input::Key>,
}

/// A [`KeyboardBinding`] represents a connection to a keyboard input device.
///
/// The [`KeyboardBinding`] parses and exposes keyboard device descriptor properties (e.g., the
/// available keyboard keys) for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to the device binding owner over `event_sender`.
pub struct KeyboardBinding {
    /// The channel to stream InputEvents to.
    event_sender: Sender<input_device::InputEvent>,

    /// Holds information about this device.
    device_descriptor: KeyboardDeviceDescriptor,
}

#[async_trait]
impl input_device::InputDeviceBinding for KeyboardBinding {
    fn input_event_sender(&self) -> Sender<input_device::InputEvent> {
        self.event_sender.clone()
    }

    fn get_device_descriptor(&self) -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Keyboard(self.device_descriptor.clone())
    }
}

impl KeyboardBinding {
    /// Creates a new [`InputDeviceBinding`] from the `device_proxy`.
    ///
    /// The binding will start listening for input reports immediately and send new InputEvents
    /// to the device binding owner over `input_event_sender`.
    ///
    /// # Parameters
    /// - `device_proxy`: The proxy to bind the new [`InputDeviceBinding`] to.
    /// - `input_event_sender`: The channel to send new InputEvents to.
    ///
    /// # Errors
    /// If there was an error binding to the proxy.
    pub async fn new(
        device_proxy: InputDeviceProxy,
        input_event_sender: Sender<input_device::InputEvent>,
    ) -> Result<Self, Error> {
        let device_binding = Self::bind_device(&device_proxy, input_event_sender).await?;
        input_device::initialize_report_stream(
            device_proxy,
            device_binding.get_device_descriptor(),
            device_binding.input_event_sender(),
            Self::process_reports,
        );

        Ok(device_binding)
    }

    /// Converts a vector of keyboard keys to the appropriate [`fidl_ui_input2::Modifiers`] bitflags.
    ///
    /// For example, if `keys` contains `Key::LeftAlt`, the bitflags will contain the corresponding
    /// flags for `LeftAlt`.
    ///
    /// # Parameters
    /// - `keys`: The keys to check for modifiers.
    ///
    /// # Returns
    /// Returns `None` if there are no modifier keys present.
    pub fn to_modifiers2(keys: &Vec<&fidl_ui_input2::Key>) -> Option<fidl_ui_input2::Modifiers> {
        let mut modifiers = fidl_ui_input2::Modifiers::empty();
        for key in keys {
            let modifier = match key {
                fidl_ui_input2::Key::LeftAlt => {
                    Some(fidl_ui_input2::Modifiers::Alt | fidl_ui_input2::Modifiers::LeftAlt)
                }
                fidl_ui_input2::Key::RightAlt => {
                    Some(fidl_ui_input2::Modifiers::Alt | fidl_ui_input2::Modifiers::RightAlt)
                }
                fidl_ui_input2::Key::LeftShift => {
                    Some(fidl_ui_input2::Modifiers::Shift | fidl_ui_input2::Modifiers::LeftShift)
                }
                fidl_ui_input2::Key::RightShift => {
                    Some(fidl_ui_input2::Modifiers::Shift | fidl_ui_input2::Modifiers::RightShift)
                }
                fidl_ui_input2::Key::LeftCtrl => Some(
                    fidl_ui_input2::Modifiers::Control | fidl_ui_input2::Modifiers::LeftControl,
                ),
                fidl_ui_input2::Key::RightCtrl => Some(
                    fidl_ui_input2::Modifiers::Control | fidl_ui_input2::Modifiers::RightControl,
                ),
                fidl_ui_input2::Key::LeftMeta => {
                    Some(fidl_ui_input2::Modifiers::Meta | fidl_ui_input2::Modifiers::LeftMeta)
                }
                fidl_ui_input2::Key::RightMeta => {
                    Some(fidl_ui_input2::Modifiers::Meta | fidl_ui_input2::Modifiers::RightMeta)
                }
                fidl_ui_input2::Key::CapsLock => Some(fidl_ui_input2::Modifiers::CapsLock),
                fidl_ui_input2::Key::NumLock => Some(fidl_ui_input2::Modifiers::NumLock),
                fidl_ui_input2::Key::ScrollLock => Some(fidl_ui_input2::Modifiers::ScrollLock),
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

    /// Converts a vector of keyboard keys to the appropriate [`fidl_ui_input3::Modifiers`] bitflags.
    ///
    /// For example, if `keys` contains `Key::CapsLock`, the bitflags will contain the corresponding
    /// flags for `CapsLock`.
    ///
    /// # Parameters
    /// - `keys`: The keys to check for modifiers.
    ///
    /// # Returns
    /// Returns `None` if there are no modifier keys present.
    pub fn to_modifiers3(
        keys: &Vec<&fidl_fuchsia_input::Key>,
    ) -> Option<fidl_ui_input3::Modifiers> {
        let mut modifiers = fidl_ui_input3::Modifiers::empty();
        for key in keys {
            let modifier = match key {
                fidl_fuchsia_input::Key::CapsLock => Some(fidl_ui_input3::Modifiers::CapsLock),
                fidl_fuchsia_input::Key::NumLock => Some(fidl_ui_input3::Modifiers::NumLock),
                fidl_fuchsia_input::Key::ScrollLock => Some(fidl_ui_input3::Modifiers::ScrollLock),
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

    /// Binds the provided input device to a new instance of `Self`.
    ///
    /// # Parameters
    /// - `device`: The device to use to initalize the binding.
    /// - `input_event_sender`: The channel to send new InputEvents to.
    ///
    /// # Errors
    /// If the device descriptor could not be retrieved, or the descriptor could not be parsed
    /// correctly.
    async fn bind_device(
        device: &InputDeviceProxy,
        input_event_sender: Sender<input_device::InputEvent>,
    ) -> Result<Self, Error> {
        match device.get_descriptor().await?.keyboard {
            Some(fidl_fuchsia_input_report::KeyboardDescriptor {
                input:
                    Some(fidl_fuchsia_input_report::KeyboardInputDescriptor { keys: keys2, keys3 }),
                output: _,
            }) => Ok(KeyboardBinding {
                event_sender: input_event_sender,
                device_descriptor: KeyboardDeviceDescriptor {
                    keys2: keys2.unwrap_or_default(),
                    keys3: keys3.unwrap_or_default(),
                },
            }),
            device_descriptor => Err(format_err!(
                "Keyboard Device Descriptor failed to parse: \n {:?}",
                device_descriptor
            )),
        }
    }

    /// Parses an [`InputReport`] into one or more [`InputEvent`]s.
    ///
    /// The [`InputEvent`]s are sent to the device binding owner via [`input_event_sender`].
    ///
    /// # Parameters
    /// `report`: The incoming [`InputReport`].
    /// `previous_report`: The previous [`InputReport`] seen for the same device. This can be
    ///                    used to determine, for example, which keys are no longer present in
    ///                    a keyboard report to generate key released events. If `None`, no
    ///                    previous report was found.
    /// `device_descriptor`: The descriptor for the input device generating the input reports.
    /// `input_event_sender`: The sender for the device binding's input event stream.
    ///
    /// # Returns
    /// An [`InputReport`] which will be passed to the next call to [`process_reports`], as
    /// [`previous_report`]. If `None`, the next call's [`previous_report`] will be `None`.
    fn process_reports(
        report: InputReport,
        previous_report: Option<InputReport>,
        device_descriptor: &input_device::InputDeviceDescriptor,
        input_event_sender: &mut Sender<input_device::InputEvent>,
    ) -> Option<InputReport> {
        // Input devices can have multiple types so ensure `report` is a KeyboardInputReport.
        match &report.keyboard {
            None => return previous_report,
            _ => (),
        };

        let new_keys2 = match KeyboardBinding::parse_pressed_keys2(&report) {
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

        let new_keys3 = match KeyboardBinding::parse_pressed_keys3(&report) {
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
        let previous_keys2: Vec<fidl_ui_input2::Key> = previous_report
            .as_ref()
            .and_then(|unwrapped_report| KeyboardBinding::parse_pressed_keys2(&unwrapped_report))
            .unwrap_or_default();
        let previous_keys3: Vec<fidl_fuchsia_input::Key> = previous_report
            .as_ref()
            .and_then(|unwrapped_report| KeyboardBinding::parse_pressed_keys3(&unwrapped_report))
            .unwrap_or_default();

        let event_time: input_device::EventTime =
            input_device::event_time_or_now(report.event_time);

        KeyboardBinding::send_key_events(
            &new_keys2,
            &new_keys3,
            &previous_keys2,
            &previous_keys3,
            device_descriptor.clone(),
            event_time,
            input_event_sender.clone(),
        );

        Some(report)
    }

    /// Parses the currently pressed [`fidl_ui_input2::Key`]s from an input report.
    ///
    /// # Parameters
    /// - `input_report`: The input report to parse the keyboard keys from.
    ///
    /// # Returns
    /// Returns `None` if any of the required input report fields are `None`. If all the
    /// required report fields are present, but there are no pressed keys, an empty vector
    /// is returned.
    fn parse_pressed_keys2(input_report: &InputReport) -> Option<Vec<fidl_ui_input2::Key>> {
        input_report
            .keyboard
            .as_ref()
            .and_then(|unwrapped_keyboard| unwrapped_keyboard.pressed_keys.as_ref())
            .and_then(|unwrapped_keys| Some(unwrapped_keys.iter().cloned().collect()))
    }

    /// Parses the currently pressed [`fidl_fuchsia_input3::Key`]s from an input report.
    ///
    /// # Parameters
    /// - `input_report`: The input report to parse the keyboard keys from.
    ///
    /// # Returns
    /// Returns `None` if any of the required input report fields are `None`. If all the
    /// required report fields are present, but there are no pressed keys, an empty vector
    /// is returned.
    fn parse_pressed_keys3(input_report: &InputReport) -> Option<Vec<fidl_fuchsia_input::Key>> {
        input_report
            .keyboard
            .as_ref()
            .and_then(|unwrapped_keyboard| unwrapped_keyboard.pressed_keys3.as_ref())
            .and_then(|unwrapped_keys| Some(unwrapped_keys.iter().cloned().collect()))
    }

    /// Sends key events to clients based on the new and previously pressed keys.
    ///
    /// # Parameters
    /// - `new_keys2`: The input2 keys which are currently pressed, as reported by the bound device.
    /// - `new_keys3`: The input3 keys which are currently pressed, as reported by the bound device.
    /// - `previous_keys2`: The input2 keys which were pressed in the previous input report.
    /// - `previous_keys3`: The input3 keys which were pressed in the previous input report.
    /// - `device_descriptor`: The descriptor for the input device generating the input reports.
    /// - `event_time`: The time in nanoseconds when the event was first recorded.
    /// - `input_event_sender`: The sender for the device binding's input event stream.
    fn send_key_events(
        new_keys2: &Vec<fidl_ui_input2::Key>,
        new_keys3: &Vec<fidl_fuchsia_input::Key>,
        previous_keys2: &Vec<fidl_ui_input2::Key>,
        previous_keys3: &Vec<fidl_fuchsia_input::Key>,
        device_descriptor: input_device::InputDeviceDescriptor,
        event_time: input_device::EventTime,
        mut input_event_sender: Sender<input_device::InputEvent>,
    ) {
        // Filter out the keys which were present in the previous keyboard report to avoid sending
        // multiple `KeyEventType::Pressed` events for a key.
        let pressed_keys2: Vec<fidl_ui_input2::Key> =
            new_keys2.iter().cloned().filter(|key| !previous_keys2.contains(key)).collect();
        let pressed_keys3: Vec<fidl_fuchsia_input::Key> =
            new_keys3.iter().cloned().filter(|key| !previous_keys3.contains(key)).collect();

        // Any key which is not present in the new keys, but was present in the previous report
        // is considered to be released.
        let released_keys2: Vec<fidl_ui_input2::Key> =
            previous_keys2.iter().cloned().filter(|key| !new_keys2.contains(key)).collect();
        let released_keys3: Vec<fidl_fuchsia_input::Key> =
            previous_keys3.iter().cloned().filter(|key| !new_keys3.contains(key)).collect();

        let keys_to_send2 = hashmap! {
            KeyEventPhase::Pressed => pressed_keys2,
            KeyEventPhase::Released => released_keys2,
        };
        let keys_to_send3 = hashmap! {
            KeyEventType::Pressed => pressed_keys3,
            KeyEventType::Released => released_keys3,
        };

        // Track any pressed modifiers.
        let modifiers2: Option<fidl_ui_input2::Modifiers> = KeyboardBinding::to_modifiers2(
            &new_keys2.iter().filter(|key| is_modifier(**key)).collect(),
        );
        let modifiers3: Option<fidl_ui_input3::Modifiers> = KeyboardBinding::to_modifiers3(
            &new_keys3.iter().filter(|key| is_modifier3(**key)).collect(),
        );

        fasync::Task::spawn(async move {
            match input_event_sender
                .send(input_device::InputEvent {
                    device_event: input_device::InputDeviceEvent::Keyboard(KeyboardEvent {
                        keys2: keys_to_send2,
                        keys3: keys_to_send3,
                        modifiers2,
                        modifiers3,
                    }),
                    device_descriptor,
                    event_time,
                })
                .await
            {
                Err(error) => {
                    fx_log_err!("Failed to send keyboard key events: {:?}", error);
                }
                _ => (),
            }
        })
        .detach();
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::testing_utilities, fuchsia_async as fasync, futures::StreamExt};

    /// Tests that a key that is present in the new report, but was not present in the previous report
    /// is propagated as pressed.
    #[fasync::run_singlethreaded(test)]
    async fn pressed_key() {
        let descriptor = input_device::InputDeviceDescriptor::Keyboard(KeyboardDeviceDescriptor {
            keys2: vec![fidl_ui_input2::Key::A],
            keys3: vec![fidl_fuchsia_input::Key::A],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![testing_utilities::create_keyboard_input_report(
            vec![fidl_ui_input2::Key::A],
            vec![fidl_fuchsia_input::Key::A],
            event_time_i64,
        )];
        let expected_events = vec![testing_utilities::create_keyboard_event(
            vec![fidl_ui_input2::Key::A],
            vec![fidl_fuchsia_input::Key::A],
            vec![],
            vec![],
            None,
            None,
            event_time_u64,
            &descriptor,
        )];

        assert_input_report_sequence_generates_events!(
            input_reports: reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: KeyboardBinding,
        );
    }

    /// Tests that a key that is not present in the new report, but was present in the previous report
    /// is propagated as released.
    #[fasync::run_singlethreaded(test)]
    async fn released_key() {
        let descriptor = input_device::InputDeviceDescriptor::Keyboard(KeyboardDeviceDescriptor {
            keys2: vec![fidl_ui_input2::Key::A],
            keys3: vec![fidl_fuchsia_input::Key::A],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![
            testing_utilities::create_keyboard_input_report(
                vec![fidl_ui_input2::Key::A],
                vec![fidl_fuchsia_input::Key::A],
                event_time_i64,
            ),
            testing_utilities::create_keyboard_input_report(vec![], vec![], event_time_i64),
        ];

        let expected_events = vec![
            testing_utilities::create_keyboard_event(
                vec![fidl_ui_input2::Key::A],
                vec![fidl_fuchsia_input::Key::A],
                vec![],
                vec![],
                None,
                None,
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![],
                vec![],
                vec![fidl_ui_input2::Key::A],
                vec![fidl_fuchsia_input::Key::A],
                None,
                None,
                event_time_u64,
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: reports,
            expected_events: expected_events,
            device_descriptor: descriptor.clone(),
            device_type: KeyboardBinding,
        );
    }

    /// Tests that a key that is present in multiple consecutive input reports is not propagated
    /// as a pressed event more than once.
    #[fasync::run_singlethreaded(test)]
    async fn multiple_pressed_event_filtering() {
        let descriptor = input_device::InputDeviceDescriptor::Keyboard(KeyboardDeviceDescriptor {
            keys2: vec![fidl_ui_input2::Key::A],
            keys3: vec![fidl_fuchsia_input::Key::A],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![
            testing_utilities::create_keyboard_input_report(
                vec![fidl_ui_input2::Key::A],
                vec![fidl_fuchsia_input::Key::A],
                event_time_i64,
            ),
            testing_utilities::create_keyboard_input_report(
                vec![fidl_ui_input2::Key::A],
                vec![fidl_fuchsia_input::Key::A],
                event_time_i64,
            ),
        ];

        let expected_events = vec![
            testing_utilities::create_keyboard_event(
                vec![fidl_ui_input2::Key::A],
                vec![fidl_fuchsia_input::Key::A],
                vec![],
                vec![],
                None,
                None,
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![],
                vec![],
                vec![],
                vec![],
                None,
                None,
                event_time_u64,
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: KeyboardBinding,
        );
    }

    /// Tests that both pressed and released keys are sent at once.
    #[fasync::run_singlethreaded(test)]
    async fn pressed_and_released_keys() {
        let descriptor = input_device::InputDeviceDescriptor::Keyboard(KeyboardDeviceDescriptor {
            keys2: vec![fidl_ui_input2::Key::A, fidl_ui_input2::Key::B],
            keys3: vec![fidl_fuchsia_input::Key::A, fidl_fuchsia_input::Key::B],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![
            testing_utilities::create_keyboard_input_report(
                vec![fidl_ui_input2::Key::A],
                vec![fidl_fuchsia_input::Key::A],
                event_time_i64,
            ),
            testing_utilities::create_keyboard_input_report(
                vec![fidl_ui_input2::Key::B],
                vec![fidl_fuchsia_input::Key::B],
                event_time_i64,
            ),
        ];

        let expected_events = vec![
            testing_utilities::create_keyboard_event(
                vec![fidl_ui_input2::Key::A],
                vec![fidl_fuchsia_input::Key::A],
                vec![],
                vec![],
                None,
                None,
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![fidl_ui_input2::Key::B],
                vec![fidl_fuchsia_input::Key::B],
                vec![fidl_ui_input2::Key::A],
                vec![fidl_fuchsia_input::Key::A],
                None,
                None,
                event_time_u64,
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: KeyboardBinding,
        );
    }

    /// Tests that input2 modifier keys are propagated to the event receiver.
    #[fasync::run_singlethreaded(test)]
    async fn input2_modifier_keys() {
        let descriptor = input_device::InputDeviceDescriptor::Keyboard(KeyboardDeviceDescriptor {
            keys2: vec![fidl_ui_input2::Key::LeftShift],
            keys3: vec![fidl_fuchsia_input::Key::LeftShift],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![testing_utilities::create_keyboard_input_report(
            vec![fidl_ui_input2::Key::LeftShift],
            vec![fidl_fuchsia_input::Key::LeftShift],
            event_time_i64,
        )];

        let expected_events = vec![testing_utilities::create_keyboard_event(
            vec![fidl_ui_input2::Key::LeftShift],
            vec![fidl_fuchsia_input::Key::LeftShift],
            vec![],
            vec![],
            Some(fidl_ui_input2::Modifiers::Shift | fidl_ui_input2::Modifiers::LeftShift),
            None, // Shift is not a supported modifier in input3
            event_time_u64,
            &descriptor,
        )];

        assert_input_report_sequence_generates_events!(
            input_reports: reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: KeyboardBinding,
        );
    }

    /// Tests that input3 modifier keys are propagated to the event receiver.
    #[fasync::run_singlethreaded(test)]
    async fn input3_modifier_keys() {
        let descriptor = input_device::InputDeviceDescriptor::Keyboard(KeyboardDeviceDescriptor {
            keys2: vec![fidl_ui_input2::Key::CapsLock],
            keys3: vec![fidl_fuchsia_input::Key::CapsLock],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![testing_utilities::create_keyboard_input_report(
            vec![fidl_ui_input2::Key::CapsLock],
            vec![fidl_fuchsia_input::Key::CapsLock],
            event_time_i64,
        )];

        let expected_events = vec![testing_utilities::create_keyboard_event(
            vec![fidl_ui_input2::Key::CapsLock],
            vec![fidl_fuchsia_input::Key::CapsLock],
            vec![],
            vec![],
            Some(fidl_ui_input2::Modifiers::CapsLock),
            Some(fidl_ui_input3::Modifiers::CapsLock),
            event_time_u64,
            &descriptor,
        )];

        assert_input_report_sequence_generates_events!(
            input_reports: reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: KeyboardBinding,
        );
    }

    /// Tests that a modifier key applies to all subsequent events until the modifier key is
    /// released.
    #[fasync::run_singlethreaded(test)]
    async fn repeated_modifier_key() {
        let descriptor = input_device::InputDeviceDescriptor::Keyboard(KeyboardDeviceDescriptor {
            keys2: vec![fidl_ui_input2::Key::CapsLock, fidl_ui_input2::Key::A],
            keys3: vec![fidl_fuchsia_input::Key::CapsLock, fidl_fuchsia_input::Key::A],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![
            testing_utilities::create_keyboard_input_report(
                vec![fidl_ui_input2::Key::CapsLock],
                vec![fidl_fuchsia_input::Key::CapsLock],
                event_time_i64,
            ),
            testing_utilities::create_keyboard_input_report(
                vec![fidl_ui_input2::Key::CapsLock, fidl_ui_input2::Key::A],
                vec![fidl_fuchsia_input::Key::CapsLock, fidl_fuchsia_input::Key::A],
                event_time_i64,
            ),
            testing_utilities::create_keyboard_input_report(
                vec![fidl_ui_input2::Key::A],
                vec![fidl_fuchsia_input::Key::A],
                event_time_i64,
            ),
        ];

        let expected_events = vec![
            testing_utilities::create_keyboard_event(
                vec![fidl_ui_input2::Key::CapsLock],
                vec![fidl_fuchsia_input::Key::CapsLock],
                vec![],
                vec![],
                Some(fidl_ui_input2::Modifiers::CapsLock),
                Some(fidl_ui_input3::Modifiers::CapsLock),
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![fidl_ui_input2::Key::A],
                vec![fidl_fuchsia_input::Key::A],
                vec![],
                vec![],
                Some(fidl_ui_input2::Modifiers::CapsLock),
                Some(fidl_ui_input3::Modifiers::CapsLock),
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![],
                vec![],
                vec![fidl_ui_input2::Key::CapsLock],
                vec![fidl_fuchsia_input::Key::CapsLock],
                None,
                None,
                event_time_u64,
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: KeyboardBinding,
        );
    }
}
