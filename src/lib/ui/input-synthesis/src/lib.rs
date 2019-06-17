// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use std::{
    thread,
    time::{Duration, SystemTime},
};

use failure::Error;

use fidl::endpoints;

use fidl_fuchsia_ui_input::{
    self, Axis, AxisScale, DeviceDescriptor, InputDeviceMarker, InputDeviceProxy,
    InputDeviceRegistryMarker, InputReport, KeyboardDescriptor, KeyboardReport,
    MediaButtonsDescriptor, MediaButtonsReport, Range, Touch, TouchscreenDescriptor,
    TouchscreenReport,
};

use fuchsia_component as app;

pub mod inverse_keymap;
pub mod keymaps;
pub mod usages;

use crate::{inverse_keymap::InverseKeymap, usages::Usages};

macro_rules! register_device {
    ( $field:ident : $value:expr ) => {{
        let mut device = DeviceDescriptor {
            device_info: None,
            keyboard: None,
            media_buttons: None,
            mouse: None,
            stylus: None,
            touchscreen: None,
            sensor: None,
        };
        device.$field = Some(Box::new($value));

        let (input_device_client, input_device_server) =
            endpoints::create_endpoints::<InputDeviceMarker>()?;
        let registry = app::client::connect_to_service::<InputDeviceRegistryMarker>()?;
        registry.register_device(&mut device, input_device_server)?;

        Ok(input_device_client.into_proxy()?)
    }};
}

fn register_touchsreen(width: u32, height: u32) -> Result<InputDeviceProxy, Error> {
    register_device! {
        touchscreen: TouchscreenDescriptor {
            x: Axis {
                range: Range { min: 0, max: width as i32 },
                resolution: 1,
                scale: AxisScale::Linear,
            },
            y: Axis {
                range: Range { min: 0, max: height as i32 },
                resolution: 1,
                scale: AxisScale::Linear,
            },
            max_finger_id: 255,
        }
    }
}

fn register_keyboard() -> Result<InputDeviceProxy, Error> {
    register_device! {
        keyboard: KeyboardDescriptor {
            keys: (Usages::HidUsageKeyA as u32..Usages::HidUsageKeyRightGui as u32).collect(),
        }
    }
}

fn register_media_buttons() -> Result<InputDeviceProxy, Error> {
    register_device! {
        media_buttons: MediaButtonsDescriptor {
            buttons: fidl_fuchsia_ui_input::MIC_MUTE
                | fidl_fuchsia_ui_input::VOLUME_DOWN
                | fidl_fuchsia_ui_input::VOLUME_UP,
        }
    }
}

fn nanos_from_epoch() -> Result<u64, Error> {
    SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .map(|duration| duration.as_nanos() as u64)
        .map_err(Into::into)
}

fn repeat_with_delay(
    times: usize,
    delay: Duration,
    f1: impl Fn(usize) -> Result<(), Error>,
    f2: impl Fn(usize) -> Result<(), Error>,
) -> Result<(), Error> {
    for i in 0..times {
        f1(i)?;
        thread::sleep(delay);
        f2(i)?;
    }

    Ok(())
}

fn media_buttons(
    volume_up: bool,
    volume_down: bool,
    mic_mute: bool,
    reset: bool,
    time: u64,
) -> InputReport {
    InputReport {
        event_time: time,
        keyboard: None,
        media_buttons: Some(Box::new(MediaButtonsReport {
            volume_up,
            volume_down,
            mic_mute,
            reset,
        })),
        mouse: None,
        stylus: None,
        touchscreen: None,
        sensor: None,
        trace_id: 0,
    }
}

/// Simulates a media button event.
pub async fn media_button_event_command(
    volume_up: bool,
    volume_down: bool,
    mic_mute: bool,
    reset: bool,
) -> Result<(), Error> {
    let input_device = register_media_buttons()?;

    input_device
        .dispatch_report(&mut media_buttons(
            volume_up,
            volume_down,
            mic_mute,
            reset,
            nanos_from_epoch()?,
        ))
        .map_err(Into::into)
}

fn tap(pos: Option<(u32, u32)>, time: u64) -> InputReport {
    InputReport {
        event_time: time,
        keyboard: None,
        media_buttons: None,
        mouse: None,
        stylus: None,
        touchscreen: Some(Box::new(TouchscreenReport {
            touches: match pos {
                Some((x, y)) => {
                    vec![Touch { finger_id: 1, x: x as i32, y: y as i32, width: 0, height: 0 }]
                }
                None => vec![],
            },
        })),
        sensor: None,
        trace_id: 0,
    }
}

/// Simulates `tap_event_count` taps at coordinates `(x, y)` for a touchscreen of explicit
/// `width` and `height`.
///
/// `duration` is divided equally between touch-down and touch-up event pairs, while the
/// transition between these pairs is immediate.
pub async fn tap_event_command(
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    duration: Duration,
    tap_event_count: usize,
) -> Result<(), Error> {
    let input_device = register_touchsreen(width, height)?;
    let tap_duration = duration / tap_event_count as u32;

    repeat_with_delay(
        tap_event_count,
        tap_duration,
        |_| {
            // Touch down.
            input_device
                .dispatch_report(&mut tap(Some((x, y)), nanos_from_epoch()?))
                .map_err(Into::into)
        },
        |_| {
            // Touch up.
            input_device.dispatch_report(&mut tap(None, nanos_from_epoch()?)).map_err(Into::into)
        },
    )
}

fn key_press(keyboard: KeyboardReport, time: u64) -> InputReport {
    InputReport {
        event_time: time,
        keyboard: Some(Box::new(keyboard)),
        media_buttons: None,
        mouse: None,
        stylus: None,
        touchscreen: None,
        sensor: None,
        trace_id: 0,
    }
}

fn key_press_usage(usage: Option<u32>, time: u64) -> InputReport {
    key_press(
        KeyboardReport {
            pressed_keys: match usage {
                Some(usage) => vec![usage],
                None => vec![],
            },
        },
        time,
    )
}

/// Simulates a key press of specified `usage`.
///
/// `duration` is the time spent between key-press and key-release events.
pub async fn key_event_command(usage: u32, duration: Duration) -> Result<(), Error> {
    let input_device = register_keyboard()?;

    repeat_with_delay(
        1,
        duration,
        |_| {
            // Key pressed.
            input_device
                .dispatch_report(&mut key_press_usage(Some(usage), nanos_from_epoch()?))
                .map_err(Into::into)
        },
        |_| {
            // Key released.
            input_device
                .dispatch_report(&mut key_press_usage(None, nanos_from_epoch()?))
                .map_err(Into::into)
        },
    )
}

/// Simulates `text` being typed on a [qwerty] keyboard by making use of [`InverseKeymap`].
///
/// `duration` is divided equally between all keyboard events.
///
/// [qwerty]: keymaps/constant.QWERTY_MAP.html
pub async fn text_command(text: String, duration: Duration) -> Result<(), Error> {
    let input_device = register_keyboard()?;
    let key_sequence = InverseKeymap::new(keymaps::QWERTY_MAP)
        .derive_key_sequence(&text)
        .ok_or_else(|| failure::err_msg("Cannot translate text to key sequence"))?;

    let stroke_duration = duration / (key_sequence.len() - 1) as u32;
    let mut key_iter = key_sequence.into_iter().peekable();

    while let Some(keyboard) = key_iter.next() {
        let result: Result<(), Error> = input_device
            .dispatch_report(&mut key_press(keyboard, nanos_from_epoch()?))
            .map_err(Into::into);
        result?;

        if key_iter.peek().is_some() {
            thread::sleep(stroke_duration);
        }
    }

    Ok(())
}

/// Simulates swipe from coordinates `(x0, y0)` to `(x1, y1)` for a touchscreen of explicit
/// `width` and `height`, with `move_event_count` touch-move events in between.
///
/// `duration` is the time spent between the touch-down and first touch-move events when
/// `move_event_count > 0` or between the touch-down the touch-up events otherwise.
pub async fn swipe_command(
    x0: u32,
    y0: u32,
    x1: u32,
    y1: u32,
    width: u32,
    height: u32,
    move_event_count: usize,
    duration: Duration,
) -> Result<(), Error> {
    let input_device = register_touchsreen(width, height)?;

    let mut delta_x = x1 as f64 - x0 as f64;
    let mut delta_y = y1 as f64 - y0 as f64;

    let swipe_event_delay = if move_event_count > 1 {
        // We have move_event_count + 2 events:
        //   DOWN
        //   MOVE x move_event_count
        //   UP
        // so we need (move_event_count + 1) delays.
        delta_x /= move_event_count as f64;
        delta_y /= move_event_count as f64;
        duration / (move_event_count + 1) as u32
    } else {
        duration
    };

    repeat_with_delay(
        move_event_count + 2,
        swipe_event_delay,
        |i| {
            let time = nanos_from_epoch()?;
            let mut report = match i {
                // DOWN
                0 => tap(Some((x0, y0)), time),
                // MOVE
                i if i <= move_event_count => tap(
                    Some((
                        x0 + (i as f64 * delta_x).round() as u32,
                        y0 + (i as f64 * delta_y).round() as u32,
                    )),
                    time,
                ),
                // UP
                _ => tap(Some((x1, y1)), time),
            };

            input_device.dispatch_report(&mut report).map_err(Into::into)
        },
        |_| Ok(()),
    )
}
