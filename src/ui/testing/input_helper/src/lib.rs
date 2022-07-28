// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_input_injection::InputDeviceRegistryProxy,
    fidl_fuchsia_input_report::{ContactInputReport, InputReport, TouchInputReport},
    fidl_fuchsia_ui_test_input::{
        RegistryRequest, RegistryRequestStream, TouchScreenRequest, TouchScreenRequestStream,
    },
    fuchsia_async as fasync,
    futures::{StreamExt, TryStreamExt},
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
            Err(e) => {
                error!("could not receive resgistry request: {:?}", e);
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
        loop {
            let request = request_stream.try_next().await;
            match request {
                Ok(Some(TouchScreenRequest::SimulateTap { payload, responder })) => {
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
                            event_time: Some(0),
                            touch: Some(touch_input_report),
                            ..InputReport::EMPTY
                        };

                        touchscreen_device
                            .send_input_report(input_report)
                            .expect("Failed to send tap input report");

                        responder.send().expect("Failed to send SimulateTap response");
                    } else {
                        warn!("SimulateTap request missing tap location");
                    }
                }
                Ok(None) => {
                    info!("Closing touchscreen channel");
                    return;
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
