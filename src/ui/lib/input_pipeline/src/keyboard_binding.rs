// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device::{self, InputDeviceBinding},
    anyhow::{format_err, Error, Result},
    async_trait::async_trait,
    fidl_fuchsia_input,
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport},
    fidl_fuchsia_ui_input3 as fidl_ui_input3,
    fidl_fuchsia_ui_input3::KeyEventType,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::{channel::mpsc::Sender, SinkExt},
    keymaps::usages::is_modifier3,
};

/// A [`KeyboardEvent`] represents an input event from a keyboard device.
///
/// The keyboard event contains information about a key event.  A key event represents a change in
/// the key state. Clients can expect the following sequence of events for a given key:
///
/// 1. [`KeyEventType::Pressed`]: the key has transitioned to being pressed.
/// 2. [`KeyEventType::Released`]: the key has transitioned to being released.
///
/// No duplicate [`KeyEventType::Pressed`] events will be sent for keys, even if the
/// key is present in a subsequent [`InputReport`]. Clients can assume that
/// a key is pressed for all received input events until the key is present in
/// the [`KeyEventType::Released`] entry of [`keys`].
#[derive(Clone, Debug, PartialEq)]
pub struct KeyboardEvent {
    /// The key that changed state in this [KeyboardEvent].
    pub key: fidl_fuchsia_input::Key,

    /// A description of what happened to `key`.
    pub event_type: KeyEventType,

    /// The [`fidl_ui_input3::Modifiers`] associated with the pressed keys.
    pub modifiers: Option<fidl_ui_input3::Modifiers>,

    /// If set, contains the unique identifier of the keymap to be used when or
    /// if remapping the keypresses.
    pub keymap: Option<String>,

    /// If set, denotes the meaning of `key` in terms of the key effect.
    /// A `KeyboardEvent` starts off with `key_meaning` unset, and the key
    /// meaning is added in the input pipeline by the appropriate
    /// keymap-aware input handlers.
    pub key_meaning: Option<fidl_fuchsia_ui_input3::KeyMeaning>,
}

/// A [`KeyboardDeviceDescriptor`] contains information about a specific keyboard device.
#[derive(Clone, Debug, PartialEq)]
pub struct KeyboardDeviceDescriptor {
    /// All the [`fidl_fuchsia_input::Key`]s available on the keyboard device.
    pub keys: Vec<fidl_fuchsia_input::Key>,
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
    pub fn to_modifiers(keys: &[&fidl_fuchsia_input::Key]) -> Option<fidl_ui_input3::Modifiers> {
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
    /// - `device`: The device to use to initialize the binding.
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
                input: Some(fidl_fuchsia_input_report::KeyboardInputDescriptor { keys3, .. }),
                output: _,
                ..
            }) => Ok(KeyboardBinding {
                event_sender: input_event_sender,
                device_descriptor: KeyboardDeviceDescriptor { keys: keys3.unwrap_or_default() },
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

        let previous_keys: Vec<fidl_fuchsia_input::Key> = previous_report
            .as_ref()
            .and_then(|unwrapped_report| KeyboardBinding::parse_pressed_keys(&unwrapped_report))
            .unwrap_or_default();

        let event_time: input_device::EventTime =
            input_device::event_time_or_now(report.event_time);

        KeyboardBinding::send_key_events(
            &new_keys,
            &previous_keys,
            device_descriptor.clone(),
            event_time,
            input_event_sender.clone(),
        );

        Some(report)
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
    fn parse_pressed_keys(input_report: &InputReport) -> Option<Vec<fidl_fuchsia_input::Key>> {
        input_report
            .keyboard
            .as_ref()
            .and_then(|unwrapped_keyboard| unwrapped_keyboard.pressed_keys3.as_ref())
            .and_then(|unwrapped_keys| Some(unwrapped_keys.iter().cloned().collect()))
    }

    /// Sends key events to clients based on the new and previously pressed keys.
    ///
    /// # Parameters
    /// - `new_keys`: The input3 keys which are currently pressed, as reported by the bound device.
    /// - `previous_keys`: The input3 keys which were pressed in the previous input report.
    /// - `device_descriptor`: The descriptor for the input device generating the input reports.
    /// - `event_time_ns`: The time in nanoseconds when the event was first recorded.
    /// - `input_event_sender`: The sender for the device binding's input event stream.
    fn send_key_events(
        new_keys: &Vec<fidl_fuchsia_input::Key>,
        previous_keys: &Vec<fidl_fuchsia_input::Key>,
        device_descriptor: input_device::InputDeviceDescriptor,
        event_time_ns: input_device::EventTime,
        input_event_sender: Sender<input_device::InputEvent>,
    ) {
        // Dispatches all key events individually in a separate task.  This is done in a separate
        // function so that the lifetime of `new_keys` above could be detached from that of the
        // spawned task.
        fn dispatch_events(
            modifiers: Option<fidl_ui_input3::Modifiers>,
            key_events: Vec<(fidl_fuchsia_input::Key, fidl_fuchsia_ui_input3::KeyEventType)>,
            device_descriptor: input_device::InputDeviceDescriptor,
            event_time: input_device::EventTime,
            mut input_event_sender: Sender<input_device::InputEvent>,
        ) {
            fasync::Task::spawn(async move {
                let mut event_time_ns = event_time;
                for (key, event_type) in key_events.into_iter() {
                    match input_event_sender
                        .send(input_device::InputEvent {
                            device_event: input_device::InputDeviceEvent::Keyboard(KeyboardEvent {
                                event_type,

                                key,
                                modifiers,
                                // At this point in the pipeline, the keymap is always unknown.
                                keymap: None,
                                // At this point in the pipeline, the key meaning is always unknown.
                                key_meaning: None,
                            }),
                            device_descriptor: device_descriptor.clone(),
                            event_time: event_time_ns,
                        })
                        .await
                    {
                        Err(error) => {
                            fx_log_err!(
                            "Failed to send KeyboardEvent for key: {:?}, event_type: {:?}: {:?}",
                            &key,
                            &event_type,
                            error
                        );
                        }
                        _ => (),
                    }
                    // If key events happen to have been reported at the same time,
                    // we pull them apart artificially. A 1ns increment will likely
                    // be enough of a difference that it is recognizable but that it
                    // does not introduce confusion.
                    event_time_ns = event_time_ns + 1;
                }
            })
            .detach();
        }

        // Track any pressed modifiers as input3 modifiers.  Note that input3
        // modifiers are very limited at the moment.
        let modifier_keys = new_keys.iter().filter(|key| is_modifier3(*key)).collect::<Vec<_>>();
        let modifiers = KeyboardBinding::to_modifiers(&modifier_keys[..]);

        // Filter out the keys which were present in the previous keyboard report to avoid sending
        // multiple `KeyEventType::Pressed` events for a key.
        let pressed_keys = new_keys
            .iter()
            .cloned()
            .filter(|key| !previous_keys.contains(key))
            .map(|k| (k, fidl_fuchsia_ui_input3::KeyEventType::Pressed));

        // Any key which is not present in the new keys, but was present in the previous report
        // is considered to be released.
        let released_keys = previous_keys
            .iter()
            .cloned()
            .filter(|key| !new_keys.contains(key))
            .map(|k| (k, fidl_fuchsia_ui_input3::KeyEventType::Released));

        // It is important that key releases are dispatched before key presses,
        // so that modifier tracking would work correctly.  We collect the result
        // into a vector since an iterator is not Send and can not be moved into
        // a closure.
        let all_keys = released_keys.chain(pressed_keys).collect::<Vec<_>>();

        dispatch_events(modifiers, all_keys, device_descriptor, event_time_ns, input_event_sender);
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
            keys: vec![fidl_fuchsia_input::Key::A],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![testing_utilities::create_keyboard_input_report(
            vec![fidl_fuchsia_input::Key::A],
            event_time_i64,
        )];
        let expected_events = vec![testing_utilities::create_keyboard_event(
            fidl_fuchsia_input::Key::A,
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            None,
            event_time_u64,
            &descriptor,
            /* keymap= */ None,
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
            keys: vec![fidl_fuchsia_input::Key::A],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![
            testing_utilities::create_keyboard_input_report(
                vec![fidl_fuchsia_input::Key::A],
                event_time_i64,
            ),
            testing_utilities::create_keyboard_input_report(vec![], event_time_i64),
        ];

        let expected_events = vec![
            testing_utilities::create_keyboard_event(
                fidl_fuchsia_input::Key::A,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_fuchsia_input::Key::A,
                fidl_fuchsia_ui_input3::KeyEventType::Released,
                None,
                event_time_u64,
                &descriptor,
                /* keymap= */ None,
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
            keys: vec![fidl_fuchsia_input::Key::A],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![
            testing_utilities::create_keyboard_input_report(
                vec![fidl_fuchsia_input::Key::A],
                event_time_i64,
            ),
            testing_utilities::create_keyboard_input_report(
                vec![fidl_fuchsia_input::Key::A],
                event_time_i64,
            ),
        ];

        let expected_events = vec![testing_utilities::create_keyboard_event(
            fidl_fuchsia_input::Key::A,
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            None,
            event_time_u64,
            &descriptor,
            /* keymap= */ None,
        )];

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
            keys: vec![fidl_fuchsia_input::Key::A, fidl_fuchsia_input::Key::B],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![
            testing_utilities::create_keyboard_input_report(
                vec![fidl_fuchsia_input::Key::A],
                event_time_i64,
            ),
            testing_utilities::create_keyboard_input_report(
                vec![fidl_fuchsia_input::Key::B],
                event_time_i64,
            ),
        ];

        let expected_events = vec![
            testing_utilities::create_keyboard_event(
                fidl_fuchsia_input::Key::A,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_fuchsia_input::Key::A,
                fidl_fuchsia_ui_input3::KeyEventType::Released,
                None,
                event_time_u64,
                &descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_fuchsia_input::Key::B,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                // Simultaneous key events are artificially separated by 1ns
                // on purpose.
                event_time_u64 + 1,
                &descriptor,
                /* keymap= */ None,
            ),
        ];

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
            keys: vec![fidl_fuchsia_input::Key::CapsLock],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![testing_utilities::create_keyboard_input_report(
            vec![fidl_fuchsia_input::Key::CapsLock],
            event_time_i64,
        )];

        let expected_events = vec![testing_utilities::create_keyboard_event(
            fidl_fuchsia_input::Key::CapsLock,
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            Some(fidl_ui_input3::Modifiers::CapsLock),
            event_time_u64,
            &descriptor,
            /* keymap= */ None,
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
            keys: vec![fidl_fuchsia_input::Key::CapsLock, fidl_fuchsia_input::Key::A],
        });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();

        let reports = vec![
            testing_utilities::create_keyboard_input_report(
                vec![fidl_fuchsia_input::Key::CapsLock],
                event_time_i64,
            ),
            testing_utilities::create_keyboard_input_report(
                vec![fidl_fuchsia_input::Key::CapsLock, fidl_fuchsia_input::Key::A],
                event_time_i64,
            ),
            testing_utilities::create_keyboard_input_report(
                vec![fidl_fuchsia_input::Key::A],
                event_time_i64,
            ),
        ];

        let expected_events = vec![
            testing_utilities::create_keyboard_event(
                fidl_fuchsia_input::Key::CapsLock,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                Some(fidl_ui_input3::Modifiers::CapsLock),
                event_time_u64,
                &descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_fuchsia_input::Key::A,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                Some(fidl_ui_input3::Modifiers::CapsLock),
                event_time_u64,
                &descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_fuchsia_input::Key::CapsLock,
                fidl_fuchsia_ui_input3::KeyEventType::Released,
                None,
                event_time_u64,
                &descriptor,
                /* keymap= */ None,
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
