// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    thread,
    time::{Duration, SystemTime},
};

use anyhow::Error;

use fidl::endpoints::{self, ServerEnd};

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

trait ServerConsumer {
    fn consume(
        &mut self,
        device: &mut DeviceDescriptor,
        server: ServerEnd<InputDeviceMarker>,
    ) -> Result<(), Error>;
}

struct RegistryServerConsumer;

impl ServerConsumer for RegistryServerConsumer {
    fn consume(
        &mut self,
        device: &mut DeviceDescriptor,
        server: ServerEnd<InputDeviceMarker>,
    ) -> Result<(), Error> {
        let registry = app::client::connect_to_service::<InputDeviceRegistryMarker>()?;
        registry.register_device(device, server)?;

        Ok(())
    }
}

macro_rules! register_device {
    ( $consumer:expr , $field:ident : $value:expr ) => {{
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
        $consumer.consume(&mut device, input_device_server)?;

        Ok(input_device_client.into_proxy()?)
    }};
}

fn register_touchscreen(
    consumer: &mut dyn ServerConsumer,
    width: u32,
    height: u32,
) -> Result<InputDeviceProxy, Error> {
    register_device! {
        consumer,
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

fn register_keyboard(consumer: &mut dyn ServerConsumer) -> Result<InputDeviceProxy, Error> {
    register_device! {
        consumer,
        keyboard: KeyboardDescriptor {
            keys: (Usages::HidUsageKeyA as u32..Usages::HidUsageKeyRightGui as u32).collect(),
        }
    }
}

fn register_media_buttons(consumer: &mut dyn ServerConsumer) -> Result<InputDeviceProxy, Error> {
    register_device! {
        consumer,
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
    pause: bool,
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
            pause,
        })),
        mouse: None,
        stylus: None,
        touchscreen: None,
        sensor: None,
        trace_id: 0,
    }
}

fn media_button_event(
    volume_up: bool,
    volume_down: bool,
    mic_mute: bool,
    reset: bool,
    pause: bool,
    consumer: &mut dyn ServerConsumer,
) -> Result<(), Error> {
    let input_device = register_media_buttons(consumer)?;

    input_device
        .dispatch_report(&mut media_buttons(
            volume_up,
            volume_down,
            mic_mute,
            reset,
            pause,
            nanos_from_epoch()?,
        ))
        .map_err(Into::into)
}

/// Simulates a media button event.
pub async fn media_button_event_command(
    volume_up: bool,
    volume_down: bool,
    mic_mute: bool,
    reset: bool,
    pause: bool,
) -> Result<(), Error> {
    media_button_event(volume_up, volume_down, mic_mute, reset, pause, &mut RegistryServerConsumer)
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

fn keyboard_event(
    usage: u32,
    duration: Duration,
    consumer: &mut dyn ServerConsumer,
) -> Result<(), Error> {
    let input_device = register_keyboard(consumer)?;

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

/// Simulates a key press of specified `usage`.
///
/// `duration` is the time spent between key-press and key-release events.
pub async fn keyboard_event_command(usage: u32, duration: Duration) -> Result<(), Error> {
    keyboard_event(usage, duration, &mut RegistryServerConsumer)
}

fn text(input: String, duration: Duration, consumer: &mut dyn ServerConsumer) -> Result<(), Error> {
    let input_device = register_keyboard(consumer)?;
    let key_sequence = InverseKeymap::new(keymaps::QWERTY_MAP)
        .derive_key_sequence(&input)
        .ok_or_else(|| anyhow::format_err!("Cannot translate text to key sequence"))?;

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

/// Simulates `input` being typed on a [qwerty] keyboard by making use of [`InverseKeymap`].
///
/// `duration` is divided equally between all keyboard events.
///
/// [qwerty]: keymaps/constant.QWERTY_MAP.html
pub async fn text_command(input: String, duration: Duration) -> Result<(), Error> {
    text(input, duration, &mut RegistryServerConsumer)
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

fn tap_event(
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    tap_event_count: usize,
    duration: Duration,
    consumer: &mut dyn ServerConsumer,
) -> Result<(), Error> {
    let input_device = register_touchscreen(consumer, width, height)?;
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
    tap_event_count: usize,
    duration: Duration,
) -> Result<(), Error> {
    tap_event(x, y, width, height, tap_event_count, duration, &mut RegistryServerConsumer)
}

fn multi_finger_tap(fingers: Option<Vec<Touch>>, time: u64) -> InputReport {
    InputReport {
        event_time: time,
        keyboard: None,
        media_buttons: None,
        mouse: None,
        stylus: None,
        touchscreen: Some(Box::new(TouchscreenReport {
            touches: match fingers {
                Some(fingers) => fingers,
                None => vec![],
            },
        })),
        sensor: None,
        trace_id: 0,
    }
}

fn multi_finger_tap_event(
    fingers: Vec<Touch>,
    width: u32,
    height: u32,
    tap_event_count: usize,
    duration: Duration,
    consumer: &mut dyn ServerConsumer,
) -> Result<(), Error> {
    let input_device = register_touchscreen(consumer, width, height)?;
    let multi_finger_tap_duration = duration / tap_event_count as u32;

    repeat_with_delay(
        tap_event_count,
        multi_finger_tap_duration,
        |_| {
            // Touch down.
            input_device
                .dispatch_report(&mut multi_finger_tap(Some(fingers.clone()), nanos_from_epoch()?))
                .map_err(Into::into)
        },
        |_| {
            // Touch up.
            input_device
                .dispatch_report(&mut multi_finger_tap(None, nanos_from_epoch()?))
                .map_err(Into::into)
        },
    )
}

/// Simulates `tap_event_count` times to repeat the multi-finger-taps, for
/// a touchscreen of explicit `width` and `height`.
///
/// `duration` is divided equally between multi-touch-down and multi-touch-up
/// pairs, while the transition between these is immediate.
pub async fn multi_finger_tap_event_command(
    fingers: Vec<Touch>,
    width: u32,
    height: u32,
    tap_event_count: usize,
    duration: Duration,
) -> Result<(), Error> {
    multi_finger_tap_event(
        fingers,
        width,
        height,
        tap_event_count,
        duration,
        &mut RegistryServerConsumer,
    )
}

fn swipe(
    x0: u32,
    y0: u32,
    x1: u32,
    y1: u32,
    width: u32,
    height: u32,
    move_event_count: usize,
    duration: Duration,
    consumer: &mut dyn ServerConsumer,
) -> Result<(), Error> {
    let input_device = register_touchscreen(consumer, width, height)?;

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
                        (x0 as f64 + (i as f64 * delta_x).round()) as u32,
                        (y0 as f64 + (i as f64 * delta_y).round()) as u32,
                    )),
                    time,
                ),
                // UP
                _ => tap(None, time),
            };

            input_device.dispatch_report(&mut report).map_err(Into::into)
        },
        |_| Ok(()),
    )
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
    swipe(x0, y0, x1, y1, width, height, move_event_count, duration, &mut RegistryServerConsumer)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Context,
        fidl_fuchsia_ui_input::{InputDeviceRequest, InputDeviceRequestStream},
        fuchsia_async as fasync,
        futures::stream::StreamExt,
    };

    // Like `InputReport`, but with the `Box`-ed items inlined.
    struct InlineInputReport {
        keyboard: Option<KeyboardReport>,
        media_buttons: Option<MediaButtonsReport>,
        touchscreen: Option<TouchscreenReport>,
    }

    impl InlineInputReport {
        fn new(input_report: InputReport) -> Self {
            Self {
                keyboard: input_report.keyboard.map(|boxed| *boxed),
                media_buttons: input_report.media_buttons.map(|boxed| *boxed),
                touchscreen: input_report.touchscreen.map(|boxed| *boxed),
            }
        }
    }
    struct TestConsumer {
        event_stream: Option<InputDeviceRequestStream>,
    }

    impl ServerConsumer for TestConsumer {
        fn consume(
            &mut self,
            _: &mut DeviceDescriptor,
            server: ServerEnd<InputDeviceMarker>,
        ) -> Result<(), Error> {
            self.event_stream = Some(server.into_stream().context("converting server to stream")?);
            Ok(())
        }
    }

    impl TestConsumer {
        fn new() -> Self {
            Self { event_stream: None }
        }

        async fn get_events(self: Self) -> Vec<Result<InlineInputReport, String>> {
            match self.event_stream {
                Some(event_stream) => {
                    event_stream
                        .map(|fidl_result| match fidl_result {
                            Ok(InputDeviceRequest::DispatchReport { report, .. }) => {
                                Ok(InlineInputReport::new(report))
                            }
                            Err(fidl_error) => Err(format!("FIDL error: {}", fidl_error)),
                        })
                        .collect()
                        .await
                }
                None => {
                    vec![Err(format!("called get_events() on Consumer with no `event_stream`"))]
                }
            }
        }
    }

    /// Transforms an `IntoIterator<Item = Result<InlineInputReport, _>>` into a
    /// `Vec<Result</* $field-specific-type */, _>>`, by projecting `$field` out of the
    /// `InlineInputReport`s.
    macro_rules! project {
        ( $events:expr, $field:ident ) => {
            $events.into_iter().map(|result| result.map(|report| report.$field)).collect::<Vec<_>>()
        };
    }

    #[fasync::run_singlethreaded(test)]
    async fn media_event_report() -> Result<(), Error> {
        let mut fake_event_listener = TestConsumer::new();
        media_button_event(true, false, true, false, true, &mut fake_event_listener)?;
        assert_eq!(
            project!(fake_event_listener.get_events().await, media_buttons),
            [Ok(Some(MediaButtonsReport {
                volume_up: true,
                volume_down: false,
                mic_mute: true,
                reset: false,
                pause: true,
            }))]
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn keyboard_event_report() -> Result<(), Error> {
        let mut fake_event_listener = TestConsumer::new();
        keyboard_event(40, Duration::from_millis(0), &mut fake_event_listener)?;
        assert_eq!(
            project!(fake_event_listener.get_events().await, keyboard),
            [
                Ok(Some(KeyboardReport { pressed_keys: vec![40] })),
                Ok(Some(KeyboardReport { pressed_keys: vec![] }))
            ]
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn text_event_report() -> Result<(), Error> {
        let mut fake_event_listener = TestConsumer::new();
        text("A".to_string(), Duration::from_millis(0), &mut fake_event_listener)?;
        assert_eq!(
            project!(fake_event_listener.get_events().await, keyboard),
            [
                Ok(Some(KeyboardReport { pressed_keys: vec![225] })),
                Ok(Some(KeyboardReport { pressed_keys: vec![4, 225] })),
                Ok(Some(KeyboardReport { pressed_keys: vec![] })),
            ]
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn multi_finger_tap_event_report() -> Result<(), Error> {
        let mut fake_event_listener = TestConsumer::new();
        let fingers = vec![
            Touch { finger_id: 1, x: 0, y: 0, width: 0, height: 0 },
            Touch { finger_id: 2, x: 20, y: 20, width: 0, height: 0 },
            Touch { finger_id: 3, x: 40, y: 40, width: 0, height: 0 },
            Touch { finger_id: 4, x: 60, y: 60, width: 0, height: 0 },
        ];
        multi_finger_tap_event(
            fingers,
            1000,
            1000,
            1,
            Duration::from_millis(0),
            &mut fake_event_listener,
        )?;
        assert_eq!(
            project!(fake_event_listener.get_events().await, touchscreen),
            [
                Ok(Some(TouchscreenReport {
                    touches: vec![
                        Touch { finger_id: 1, x: 0, y: 0, width: 0, height: 0 },
                        Touch { finger_id: 2, x: 20, y: 20, width: 0, height: 0 },
                        Touch { finger_id: 3, x: 40, y: 40, width: 0, height: 0 },
                        Touch { finger_id: 4, x: 60, y: 60, width: 0, height: 0 },
                    ],
                })),
                Ok(Some(TouchscreenReport { touches: vec![] })),
            ]
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn tap_event_report() -> Result<(), Error> {
        let mut fake_event_listener = TestConsumer::new();
        tap_event(10, 10, 1000, 1000, 1, Duration::from_millis(0), &mut fake_event_listener)?;
        assert_eq!(
            project!(fake_event_listener.get_events().await, touchscreen),
            [
                Ok(Some(TouchscreenReport {
                    touches: vec![Touch { finger_id: 1, x: 10, y: 10, width: 0, height: 0 }]
                })),
                Ok(Some(TouchscreenReport { touches: vec![] })),
            ]
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn swipe_event_report() -> Result<(), Error> {
        let mut fake_event_listener = TestConsumer::new();
        swipe(10, 10, 100, 100, 1000, 1000, 2, Duration::from_millis(0), &mut fake_event_listener)?;
        assert_eq!(
            project!(fake_event_listener.get_events().await, touchscreen),
            [
                Ok(Some(TouchscreenReport {
                    touches: vec![Touch { finger_id: 1, x: 10, y: 10, width: 0, height: 0 }],
                })),
                Ok(Some(TouchscreenReport {
                    touches: vec![Touch { finger_id: 1, x: 55, y: 55, width: 0, height: 0 }],
                })),
                Ok(Some(TouchscreenReport {
                    touches: vec![Touch { finger_id: 1, x: 100, y: 100, width: 0, height: 0 }],
                })),
                Ok(Some(TouchscreenReport { touches: vec![] })),
            ]
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn swipe_event_report_inverted() -> Result<(), Error> {
        let mut fake_event_listener = TestConsumer::new();
        swipe(100, 100, 10, 10, 1000, 1000, 2, Duration::from_millis(0), &mut fake_event_listener)?;
        assert_eq!(
            project!(fake_event_listener.get_events().await, touchscreen),
            [
                Ok(Some(TouchscreenReport {
                    touches: vec![Touch { finger_id: 1, x: 100, y: 100, width: 0, height: 0 }],
                })),
                Ok(Some(TouchscreenReport {
                    touches: vec![Touch { finger_id: 1, x: 55, y: 55, width: 0, height: 0 }],
                })),
                Ok(Some(TouchscreenReport {
                    touches: vec![Touch { finger_id: 1, x: 10, y: 10, width: 0, height: 0 }],
                })),
                Ok(Some(TouchscreenReport { touches: vec![] })),
            ]
        );
        Ok(())
    }
}
