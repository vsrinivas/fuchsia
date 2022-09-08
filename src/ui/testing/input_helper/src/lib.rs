// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_input::Key,
    fidl_fuchsia_input_injection::InputDeviceRegistryProxy,
    fidl_fuchsia_input_report::{
        ConsumerControlInputReport, ContactInputReport, DeviceInfo, InputReport,
        KeyboardInputReport, TouchInputReport,
    },
    fidl_fuchsia_math as math,
    fidl_fuchsia_ui_input::KeyboardReport,
    fidl_fuchsia_ui_test_input::{
        KeyboardRequest, KeyboardRequestStream, MediaButtonsDeviceRequest,
        MediaButtonsDeviceRequestStream, RegistryRequest, RegistryRequestStream,
        TouchScreenRequest, TouchScreenRequestStream,
    },
    fuchsia_async as fasync,
    futures::StreamExt,
    keymaps::{
        inverse_keymap::{InverseKeymap, Shift},
        usages::{hid_usage_to_input3_key, Usages},
    },
    std::time::Duration,
    tracing::{error, info, warn},
};

mod input_device;
mod input_device_registry;
mod input_reports_reader;

/// Use this to place required DeviceInfo into DeviceDescriptor.
fn new_fake_device_info() -> DeviceInfo {
    DeviceInfo { product_id: 42, vendor_id: 43, version: u32::MAX }
}

/// Converts the `input` string into a key sequence under the `InverseKeymap` derived from `keymap`.
///
/// This is intended for end-to-end and input testing only; for production use cases and general
/// testing, IME injection should be used instead.
///
/// A translation from `input` to a sequence of keystrokes is not guaranteed to exist. If a
/// translation does not exist, `None` is returned.
///
/// The sequence does not contain pauses except between repeated keys or to clear a shift state,
/// though the sequence does terminate with an empty report (no keys pressed). A shift key
/// transition is sent in advance of each series of keys that needs it.
///
/// Note that there is currently no way to distinguish between particular key releases. As such,
/// only one key release report is generated even in combinations, e.g. Shift + A.
///
/// # Example
///
/// ```
/// let key_sequence = derive_key_sequence(&keymaps::US_QWERTY, "A").unwrap();
///
/// // [shift, A, clear]
/// assert_eq!(key_sequence.len(), 3);
/// ```
///
/// TODO(fxbug.dev/108530): Simplify the logic in this test.
fn derive_key_sequence(keymap: &keymaps::Keymap<'_>, input: &str) -> Option<Vec<KeyboardReport>> {
    let inverse_keymap = InverseKeymap::new(keymap);
    let mut reports = vec![];
    let mut shift_pressed = false;
    let mut last_usage = None;

    for ch in input.chars() {
        let key_stroke = inverse_keymap.get(&ch)?;

        match key_stroke.shift {
            Shift::Yes if !shift_pressed => {
                shift_pressed = true;
                last_usage = Some(0);
            }
            Shift::No if shift_pressed => {
                shift_pressed = false;
                last_usage = Some(0);
            }
            _ => {
                if last_usage == Some(key_stroke.usage) {
                    last_usage = Some(0);
                }
            }
        }

        if let Some(0) = last_usage {
            reports.push(KeyboardReport {
                pressed_keys: if shift_pressed {
                    vec![Usages::HidUsageKeyLeftShift as u32]
                } else {
                    vec![]
                },
            });
        }

        last_usage = Some(key_stroke.usage);

        reports.push(KeyboardReport {
            pressed_keys: if shift_pressed {
                vec![key_stroke.usage, Usages::HidUsageKeyLeftShift as u32]
            } else {
                vec![key_stroke.usage]
            },
        });
    }

    reports.push(KeyboardReport { pressed_keys: vec![] });

    Some(reports)
}

fn convert_keyboard_report_to_keys(report: &KeyboardReport) -> Vec<Key> {
    report
        .pressed_keys
        .iter()
        .map(|&usage| {
            hid_usage_to_input3_key(usage.try_into().expect("failed to convert usage to u16"))
                .expect(format!("no Key for usage {:?}", usage).as_str())
        })
        .collect()
}

/// Serves `fuchsia.ui.test.input.Registry`.
pub async fn handle_registry_request_stream(
    mut request_stream: RegistryRequestStream,
    input_device_registry: InputDeviceRegistryProxy,
) {
    let mut registry = input_device_registry::InputDeviceRegistry::new(input_device_registry);
    while let Some(request) = request_stream.next().await {
        match request {
            Ok(RegistryRequest::RegisterTouchScreen { payload, responder, .. }) => {
                info!("register touchscreen");

                if let Some(device) = payload.device {
                    let touchscreen_device = registry
                        .add_touchscreen_device()
                        .expect("failed to create fake touchscreen device");

                    handle_touchscreen_request_stream(
                        touchscreen_device,
                        device
                            .into_stream()
                            .expect("failed to convert touchscreen device to stream"),
                    );
                } else {
                    error!("no touchscreen device provided in registration request");
                }

                responder.send().expect("Failed to respond to RegisterTouchScreen request");
            }
            Ok(RegistryRequest::RegisterMediaButtonsDevice { payload, responder, .. }) => {
                info!("register media buttons device");

                if let Some(device) = payload.device {
                    let media_buttons_device = registry
                        .add_media_buttons_device()
                        .expect("failed to create fake media buttons device");

                    handle_media_buttons_device_request_stream(
                        media_buttons_device,
                        device
                            .into_stream()
                            .expect("failed to convert media buttons device to stream"),
                    );
                } else {
                    error!("no media buttons device provided in registration request");
                }

                responder.send().expect("Failed to respond to RegisterMediaButtonsDevice request");
            }
            Ok(RegistryRequest::RegisterKeyboard { payload, responder, .. }) => {
                info!("register keyboard device");

                if let Some(device) = payload.device {
                    let keyboard_device = registry
                        .add_keyboard_device()
                        .expect("failed to create fake keyboard device");

                    handle_keyboard_request_stream(
                        keyboard_device,
                        device.into_stream().expect("failed to convert keyboard device to stream"),
                    );
                } else {
                    error!("no keyboard device provided in registration request");
                }

                responder.send().expect("Failed to respond to RegisterKeyboard request");
            }
            Err(e) => {
                error!("could not receive registry request: {:?}", e);
            }
        }
    }
}

fn input_report_for_touch_contacts(contacts: Vec<(u32, math::Vec_)>) -> InputReport {
    let contact_input_reports = contacts
        .into_iter()
        .map(|(contact_id, location)| ContactInputReport {
            contact_id: Some(contact_id),
            position_x: Some(location.x as i64),
            position_y: Some(location.y as i64),
            contact_width: Some(0),
            contact_height: Some(0),
            ..ContactInputReport::EMPTY
        })
        .collect();

    let touch_input_report = TouchInputReport {
        contacts: Some(contact_input_reports),
        pressed_buttons: Some(vec![]),
        ..TouchInputReport::EMPTY
    };

    InputReport {
        event_time: Some(fasync::Time::now().into_nanos()),
        touch: Some(touch_input_report),
        ..InputReport::EMPTY
    }
}

/// Serves `fuchsia.ui.test.input.TouchScreen`.
fn handle_touchscreen_request_stream(
    touchscreen_device: input_device::InputDevice,
    mut request_stream: TouchScreenRequestStream,
) {
    fasync::Task::local(async move {
        while let Some(request) = request_stream.next().await {
            match request {
                Ok(TouchScreenRequest::SimulateTap { payload, responder }) => {
                    if let Some(tap_location) = payload.tap_location {
                        touchscreen_device
                            .send_input_report(input_report_for_touch_contacts(vec![(
                                1,
                                tap_location,
                            )]))
                            .expect("Failed to send tap input report");

                        // Send a report with an empty set of touch contacts, so that input
                        // pipeline generates a pointer event with phase == UP.
                        touchscreen_device
                            .send_input_report(input_report_for_touch_contacts(vec![]))
                            .expect("failed to send empty input report");

                        responder.send().expect("Failed to send SimulateTap response");
                    } else {
                        warn!("SimulateTap request missing tap location");
                    }
                }
                Ok(TouchScreenRequest::SimulateSwipe { payload, responder }) => {
                    // Compute the x- and y- displacements between successive touch events.
                    let start_location = payload.start_location.expect("missing start location");
                    let end_location = payload.end_location.expect("missing end location");
                    let move_event_count =
                        payload.move_event_count.expect("missing move event count");

                    let start_x_f = start_location.x as f64;
                    let start_y_f = start_location.y as f64;
                    let end_x_f = end_location.x as f64;
                    let end_y_f = end_location.y as f64;
                    let move_event_count_f = move_event_count as f64;
                    let step_size_x = (end_x_f - start_x_f) / move_event_count_f;
                    let step_size_y = (end_y_f - start_y_f) / move_event_count_f;

                    // Generate an event at `start_location`, followed by `move_event_count - 1`
                    // evenly-spaced events, followed by an event at `end_location`.
                    for i in 0..move_event_count + 1 {
                        let i_f = i as f64;
                        let event_x = start_x_f + (i_f * step_size_x);
                        let event_y = start_y_f + (i_f * step_size_y);
                        touchscreen_device
                            .send_input_report(input_report_for_touch_contacts(vec![(
                                1,
                                math::Vec_ { x: event_x as i32, y: event_y as i32 },
                            )]))
                            .expect("Failed to send tap input report");
                    }

                    // Send a report with an empty set of touch contacts, so that input
                    // pipeline generates a pointer event with phase == UP.
                    touchscreen_device
                        .send_input_report(input_report_for_touch_contacts(vec![]))
                        .expect("failed to send empty input report");

                    responder.send().expect("Failed to send SimulateSwipe response");
                }
                Err(e) => {
                    error!("Error on touchscreen channel: {}", e);
                    return;
                }
            }
        }
    })
    .detach()
}

/// Serves `fuchsia.ui.test.input.MediaButtonsDevice`.
fn handle_media_buttons_device_request_stream(
    media_buttons_device: input_device::InputDevice,
    mut request_stream: MediaButtonsDeviceRequestStream,
) {
    fasync::Task::local(async move {
        while let Some(request) = request_stream.next().await {
            match request {
                Ok(MediaButtonsDeviceRequest::SimulateButtonPress { payload, responder }) => {
                    if let Some(button) = payload.button {
                        let media_buttons_input_report = ConsumerControlInputReport {
                            pressed_buttons: Some(vec![button]),
                            ..ConsumerControlInputReport::EMPTY
                        };

                        let input_report = InputReport {
                            event_time: Some(fasync::Time::now().into_nanos()),
                            consumer_control: Some(media_buttons_input_report),
                            ..InputReport::EMPTY
                        };

                        media_buttons_device
                            .send_input_report(input_report)
                            .expect("Failed to send button press input report");

                        // Send a report with an empty set of pressed buttons,
                        // so that input pipeline generates a media buttons
                        // event with the target button being released.
                        let empty_report = InputReport {
                            event_time: Some(fasync::Time::now().into_nanos()),
                            consumer_control: Some(ConsumerControlInputReport {
                                pressed_buttons: Some(vec![]),
                                ..ConsumerControlInputReport::EMPTY
                            }),
                            ..InputReport::EMPTY
                        };

                        media_buttons_device
                            .send_input_report(empty_report)
                            .expect("Failed to send button release input report");

                        responder.send().expect("Failed to send SimulateButtonPress response");
                    } else {
                        warn!("SimulateButtonPress request missing button");
                    }
                }
                Err(e) => {
                    error!("Error on media buttons device channel: {}", e);
                    return;
                }
            }
        }
    })
    .detach()
}

/// Serves `fuchsia.ui.test.input.Keyboard`.
fn handle_keyboard_request_stream(
    keyboard_device: input_device::InputDevice,
    mut request_stream: KeyboardRequestStream,
) {
    fasync::Task::local(async move {
        while let Some(request) = request_stream.next().await {
            match request {
                Ok(KeyboardRequest::SimulateUsAsciiTextEntry { payload, responder }) => {
                    if let Some(text) = payload.text {
                        let key_sequence = derive_key_sequence(&keymaps::US_QWERTY, &text)
                            .expect("Failed to derive key sequence");

                        let mut key_iter = key_sequence.into_iter().peekable();
                        while let Some(keyboard_report) = key_iter.next() {
                            let input_report = InputReport {
                                event_time: Some(fasync::Time::now().into_nanos()),
                                keyboard: Some(KeyboardInputReport {
                                    pressed_keys3: Some(convert_keyboard_report_to_keys(
                                        &keyboard_report,
                                    )),
                                    ..KeyboardInputReport::EMPTY
                                }),
                                ..InputReport::EMPTY
                            };

                            keyboard_device
                                .send_input_report(input_report)
                                .expect("Failed to send key event report");

                            if key_iter.peek().is_some() {
                                fuchsia_async::Timer::new(Duration::from_millis(100)).await;
                            }
                        }

                        responder.send().expect("Failed to send SimulateTextEntry response");
                    } else {
                        warn!("SimulateTextEntry request missing text");
                    }
                }
                Err(e) => {
                    error!("Error on keyboard device channel: {}", e);
                    return;
                }
            }
        }
    })
    .detach()
}

#[cfg(test)]
mod tests {
    // Most of the functions in this file need to bind to FIDL services in
    // this component's environment to do their work, but a component can't
    // modify its own environment. Hence, we can't validate those functions.
    //
    // However, we can (and do) validate derive_key_sequence().

    use super::{derive_key_sequence, KeyboardReport, Usages};
    use pretty_assertions::assert_eq;

    // TODO(fxbug.dev/108530): Remove this macro.
    macro_rules! reports {
        ( $( [ $( $usages:expr ),* ] ),* $( , )? ) => {
            Some(vec![
                $(
                    KeyboardReport {
                        pressed_keys: vec![$($usages as u32),*]
                    }
                ),*
            ])
        }
    }

    #[test]
    fn lowercase() {
        assert_eq!(
            derive_key_sequence(&keymaps::US_QWERTY, "lowercase"),
            reports![
                [Usages::HidUsageKeyL],
                [Usages::HidUsageKeyO],
                [Usages::HidUsageKeyW],
                [Usages::HidUsageKeyE],
                [Usages::HidUsageKeyR],
                [Usages::HidUsageKeyC],
                [Usages::HidUsageKeyA],
                [Usages::HidUsageKeyS],
                [Usages::HidUsageKeyE],
                [],
            ]
        );
    }

    #[test]
    fn numerics() {
        assert_eq!(
            derive_key_sequence(&keymaps::US_QWERTY, "0123456789"),
            reports![
                [Usages::HidUsageKey0],
                [Usages::HidUsageKey1],
                [Usages::HidUsageKey2],
                [Usages::HidUsageKey3],
                [Usages::HidUsageKey4],
                [Usages::HidUsageKey5],
                [Usages::HidUsageKey6],
                [Usages::HidUsageKey7],
                [Usages::HidUsageKey8],
                [Usages::HidUsageKey9],
                [],
            ]
        );
    }

    #[test]
    fn internet_text_entry() {
        assert_eq!(
            derive_key_sequence(&keymaps::US_QWERTY, "http://127.0.0.1:8080"),
            reports![
                [Usages::HidUsageKeyH],
                [Usages::HidUsageKeyT],
                [],
                [Usages::HidUsageKeyT],
                [Usages::HidUsageKeyP],
                // ':'
                // Shift is actuated first on its own, then together with
                // the key.
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeySemicolon, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKeySlash],
                [],
                [Usages::HidUsageKeySlash],
                [Usages::HidUsageKey1],
                [Usages::HidUsageKey2],
                [Usages::HidUsageKey7],
                [Usages::HidUsageKeyDot],
                [Usages::HidUsageKey0],
                [Usages::HidUsageKeyDot],
                [Usages::HidUsageKey0],
                [Usages::HidUsageKeyDot],
                [Usages::HidUsageKey1],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeySemicolon, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKey8],
                [Usages::HidUsageKey0],
                [Usages::HidUsageKey8],
                [Usages::HidUsageKey0],
                [],
            ]
        );
    }

    #[test]
    fn sentence() {
        assert_eq!(
            derive_key_sequence(&keymaps::US_QWERTY, "Hello, world!"),
            reports![
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyH, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKeyE],
                [Usages::HidUsageKeyL],
                [],
                [Usages::HidUsageKeyL],
                [Usages::HidUsageKeyO],
                [Usages::HidUsageKeyComma],
                [Usages::HidUsageKeySpace],
                [Usages::HidUsageKeyW],
                [Usages::HidUsageKeyO],
                [Usages::HidUsageKeyR],
                [Usages::HidUsageKeyL],
                [Usages::HidUsageKeyD],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKey1, Usages::HidUsageKeyLeftShift],
                [],
            ]
        );
    }

    #[test]
    fn hold_shift() {
        assert_eq!(
            derive_key_sequence(&keymaps::US_QWERTY, "ALL'S WELL!"),
            reports![
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyA, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyL, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyL, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKeyApostrophe],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyS, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeySpace, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyW, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyE, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyL, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyL, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKey1, Usages::HidUsageKeyLeftShift],
                [],
            ]
        );
    }

    #[test]
    fn tab_and_newline() {
        assert_eq!(
            derive_key_sequence(&keymaps::US_QWERTY, "\tHello\n"),
            reports![
                [Usages::HidUsageKeyTab],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyH, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKeyE],
                [Usages::HidUsageKeyL],
                [],
                [Usages::HidUsageKeyL],
                [Usages::HidUsageKeyO],
                [Usages::HidUsageKeyEnter],
                [],
            ]
        );
    }
}
