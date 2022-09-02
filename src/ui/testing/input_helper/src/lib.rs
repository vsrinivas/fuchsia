// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_input_injection::InputDeviceRegistryProxy,
    fidl_fuchsia_input_report::{
        ConsumerControlInputReport, ContactInputReport, InputReport, TouchInputReport,
    },
    fidl_fuchsia_ui_test_input::{
        MediaButtonsDeviceRequest, MediaButtonsDeviceRequestStream, RegistryRequest,
        RegistryRequestStream, TouchScreenRequest, TouchScreenRequestStream,
    },
    fuchsia_async as fasync,
    futures::StreamExt,
    tracing::{error, info, warn},
};

mod input_device;
mod input_device_registry;
mod input_reports_reader;

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
            Ok(RegistryRequest::RegisterKeyboard { .. }) => {
                // TODO(fxbug.dev/108059): Implement.
                error!("keyboard not yet supported");
            }
            Err(e) => {
                error!("could not receive registry request: {:?}", e);
            }
        }
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
                        let touch_input_report = TouchInputReport {
                            contacts: Some(vec![ContactInputReport {
                                contact_id: Some(1),
                                position_x: Some(tap_location.x as i64),
                                position_y: Some(tap_location.y as i64),
                                contact_width: Some(0),
                                contact_height: Some(0),
                                ..ContactInputReport::EMPTY
                            }]),
                            pressed_buttons: Some(vec![]),
                            ..TouchInputReport::EMPTY
                        };

                        let input_report = InputReport {
                            event_time: Some(fasync::Time::now().into_nanos()),
                            touch: Some(touch_input_report),
                            ..InputReport::EMPTY
                        };

                        touchscreen_device
                            .send_input_report(input_report)
                            .expect("Failed to send tap input report");

                        // Send a report with an empty set of touch contacts, so that input
                        // pipeline generates a pointer event with phase == UP.
                        let empty_report = InputReport {
                            event_time: Some(fasync::Time::now().into_nanos()),
                            touch: Some(TouchInputReport {
                                contacts: Some(vec![]),
                                pressed_buttons: Some(vec![]),
                                ..TouchInputReport::EMPTY
                            }),
                            ..InputReport::EMPTY
                        };

                        touchscreen_device
                            .send_input_report(empty_report)
                            .expect("Failed to send tap input report");

                        responder.send().expect("Failed to send SimulateTap response");
                    } else {
                        warn!("SimulateTap request missing tap location");
                    }
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
