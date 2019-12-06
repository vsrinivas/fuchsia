// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error};
use futures::select;
use futures::stream::{FuturesUnordered, StreamExt};
use {fuchsia_syslog::fx_log_info, input};

/// Await a touch event and print it.
/// There may be more than one touch device, so return the device that reported the event so the
/// caller can request the next event. This way the caller doesn't need to maintain a mapping of
/// event handlers to input devices.
///
/// # Parameters
/// - `input`: the input device
async fn handle_touches(input: input::InputDeviceWithType) -> input::InputDeviceWithType {
    let input_reports = input.device.get_reports().await;
    // fx_log_info!("got touches ???? ({:?})!", input.report_path);
    // We get this far but no farther many times a second
    for input_report in input_reports.unwrap_or_else(|_| vec![]) {
        if let Some(ref touch_report) = input_report.touch {
            if let Some(ref contacts) = touch_report.contacts {
                for contact_report in contacts {
                    let id = contact_report.contact_id.unwrap_or(0);
                    let x = contact_report.position_x.unwrap_or(0);
                    let y = contact_report.position_y.unwrap_or(0);
                    // other fields: pressure, contact_width, contact_height
                    fx_log_info!("touch: {}, X: {}, Y: {}", id, x, y);
                }
            }
        }
    }
    input
}

/// Await a mouse event and print it.
/// There may be more than one mouse device, so return the device that reported the event so the
/// caller can request the next event. This way the caller doesn't need to maintain a mapping of
/// event handlers to input devices.
///
/// Some mouse devices report touch events only; not mouse events.
///
/// # Parameters
/// - `input`: the input device
async fn handle_mouses(input: input::InputDeviceWithType) -> input::InputDeviceWithType {
    let input_reports = input.device.get_reports().await;
    // fx_log_info!("got mouses ???? ({:?})!", input.report_path);
    // We get this far but no farther many times a second
    for input_report in input_reports.unwrap_or_else(|_| vec![]) {
        if let Some(ref mouse_report) = input_report.mouse {
            let x = mouse_report.movement_x.unwrap_or(0);
            let y = mouse_report.movement_y.unwrap_or(0);
            let scroll_v = mouse_report.scroll_v.unwrap_or(0);
            fx_log_info!("mouse: X: {}, Y: {}, scroll_v: {}", x, y, scroll_v);
        // other fields: scroll_h, buttons
        } else if let Some(ref touch_report) = input_report.touch {
            // Currently, the mouse device produces touch events (is that a system configuration?)
            if let Some(ref contacts) = touch_report.contacts {
                for contact_report in contacts {
                    let id = contact_report.contact_id.unwrap_or(0);
                    let x = contact_report.position_x.unwrap_or(0);
                    let y = contact_report.position_y.unwrap_or(0);
                    // other fields: pressure, contact_width, contact_height
                    fx_log_info!("mouse (as touch): {}, X: {}, Y: {}", id, x, y);
                }
            }
        }
    }
    input
}

/// Await a keyboard event and print it.
/// There may be more than one keyboard device, so return the device that reported the event so the
/// caller can request the next event. This way the caller doesn't need to maintain a mapping of
/// event handlers to input devices.
///
/// # Parameters
/// - `input`: the input device
async fn handle_keys(input: input::InputDeviceWithType) -> input::InputDeviceWithType {
    let input_reports = input.device.get_reports().await;
    // fx_log_info!("got keys ???? ({:?})!", input.report_path);
    // We get this far but no farther many times a second (on all given keyboards)
    // PROBLEM:  WHY IS THIS FUTURE COMPLETING WITHOUT AN EVENT WITH ACTUAL KEYS?
    for input_report in input_reports.unwrap_or_else(|_| vec![]) {
        //fx_log_info!("got key input_report ????");
        if let Some(ref keyboard_report) = input_report.keyboard {
            //fx_log_info!("got keyboard_report ????");
            if let Some(ref pressed_keys) = keyboard_report.pressed_keys {
                //fx_log_info!("got pressed_keys ????");
                if pressed_keys.len() == 0 {
                    if let Ok(descriptor) = input.device.get_descriptor().await {
                        if descriptor.keyboard.is_some() {
                            // THIS HAPPENS AFTER EVERY KEY EVENT... WHY?
                            // fx_log_info!("got pressed_keys with zero keys ({:?})!",
                            // input.report_path);
                        } else {
                            fx_log_info!(
                                "got pressed_keys with no descriptor keyboard! ({:?})!",
                                input.report_path
                            );
                        }
                    } else {
                        fx_log_info!(
                            "got pressed_keys with no descriptor! ({:?})!",
                            input.report_path
                        );
                    }
                }
                for pressed_key in pressed_keys {
                    // Log the key pressed. |pressed_key| is an enum corresponding to a physical
                    // key, like 'A', 'S', 'D', 'F', etc. This ignores concepts like Shift-'A' to
                    // capitalize, or other special handling for things like Accessibility and IME.
                    // fx_log_info!("pressed key id: {:?} ({:?})!,", pressed_key,
                    // input.report_path);
                    fx_log_info!("key: {:?}", pressed_key);
                }
            }
        }
    }
    input
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["input_session"]).expect("Failed to initialize logger.");

    let mut touch_futures = FuturesUnordered::new();
    let mut mouse_futures = FuturesUnordered::new();
    let mut key_futures = FuturesUnordered::new();

    let inputs: Vec<input::InputDeviceWithType> = input::get_input_devices().await?;
    fx_log_info!("got {} inputs", inputs.len());
    for input in inputs {
        fx_log_info!("Found {:?} at path {}", input.device_type, input.report_path);
        match input.device_type {
            input::InputDeviceType::Touch => {
                touch_futures.push(handle_touches(input));
            }
            input::InputDeviceType::Mouse => {
                mouse_futures.push(handle_mouses(input));
            }
            input::InputDeviceType::Keyboard => {
                key_futures.push(handle_keys(input));
            }
        }
    }

    loop {
        select! {
            // Each handler returns the device that it handled so we can request the next event.
            input = touch_futures.select_next_some() => {
                touch_futures.push(handle_touches(input));
            },
            input = mouse_futures.select_next_some() => {
                mouse_futures.push(handle_mouses(input));
            },
            input = key_futures.select_next_some() => {
                key_futures.push(handle_keys(input));
            },
            complete => {
                return Err(format_err!("input device loop completed unexpectedly"))
            },
        }
    }
}
