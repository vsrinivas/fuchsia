// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{inverse_keymap::InverseKeymap, keymaps},
    anyhow::{ensure, Error},
    fidl_fuchsia_ui_input::{self, KeyboardReport, Touch},
    std::{
        convert::TryFrom,
        thread,
        time::{Duration, SystemTime},
    },
};

// Abstracts over input injection services.
pub(crate) trait Injector {
    fn make_touchscreen_device(
        &mut self,
        width: u32,
        height: u32,
    ) -> Result<Box<dyn InputDevice>, Error>;
    fn make_keyboard_device(&mut self) -> Result<Box<dyn InputDevice>, Error>;
    fn make_media_buttons_device(&mut self) -> Result<Box<dyn InputDevice>, Error>;
}

// Abstracts over the various interactions that a user might have with an input device.
// Note that the input-synthesis crate deliberately chooses not to "sub-type" input devices.
// This avoids additional code complexity, and also the crate to support tests that
// deliberately send events that do not match the expected event type for a device.
pub(crate) trait InputDevice {
    fn media_buttons(
        &self,
        volume_up: bool,
        volume_down: bool,
        mic_mute: bool,
        reset: bool,
        pause: bool,
        time: u64,
    ) -> Result<(), Error>;
    fn key_press(&self, keyboard: KeyboardReport, time: u64) -> Result<(), Error>;
    fn key_press_usage(&self, usage: Option<u32>, time: u64) -> Result<(), Error>;
    fn tap(&self, pos: Option<(u32, u32)>, time: u64) -> Result<(), Error>;
    fn multi_finger_tap(&self, fingers: Option<Vec<Touch>>, time: u64) -> Result<(), Error>;
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

pub(crate) fn media_button_event(
    volume_up: bool,
    volume_down: bool,
    mic_mute: bool,
    reset: bool,
    pause: bool,
    injector: &mut dyn Injector,
) -> Result<(), Error> {
    injector.make_media_buttons_device()?.media_buttons(
        volume_up,
        volume_down,
        mic_mute,
        reset,
        pause,
        nanos_from_epoch()?,
    )
}

pub(crate) fn keyboard_event(
    usage: u32,
    duration: Duration,
    injector: &mut dyn Injector,
) -> Result<(), Error> {
    let input_device = injector.make_keyboard_device()?;

    repeat_with_delay(
        1,
        duration,
        |_| {
            // Key pressed.
            input_device.key_press_usage(Some(usage), nanos_from_epoch()?)
        },
        |_| {
            // Key released.
            input_device.key_press_usage(None, nanos_from_epoch()?)
        },
    )
}

pub(crate) fn text(
    input: String,
    duration: Duration,
    injector: &mut dyn Injector,
) -> Result<(), Error> {
    let input_device = injector.make_keyboard_device()?;
    let key_sequence = InverseKeymap::new(keymaps::QWERTY_MAP)
        .derive_key_sequence(&input)
        .ok_or_else(|| anyhow::format_err!("Cannot translate text to key sequence"))?;

    let stroke_duration = duration / (key_sequence.len() - 1) as u32;
    let mut key_iter = key_sequence.into_iter().peekable();

    while let Some(keyboard) = key_iter.next() {
        let result: Result<(), Error> = input_device.key_press(keyboard, nanos_from_epoch()?);
        result?;

        if key_iter.peek().is_some() {
            thread::sleep(stroke_duration);
        }
    }

    Ok(())
}

pub(crate) fn tap_event(
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    tap_event_count: usize,
    duration: Duration,
    injector: &mut dyn Injector,
) -> Result<(), Error> {
    let input_device = injector.make_touchscreen_device(width, height)?;
    let tap_duration = duration / tap_event_count as u32;

    repeat_with_delay(
        tap_event_count,
        tap_duration,
        |_| {
            // Touch down.
            input_device.tap(Some((x, y)), nanos_from_epoch()?)
        },
        |_| {
            // Touch up.
            input_device.tap(None, nanos_from_epoch()?)
        },
    )
}

pub(crate) fn multi_finger_tap_event(
    fingers: Vec<Touch>,
    width: u32,
    height: u32,
    tap_event_count: usize,
    duration: Duration,
    injector: &mut dyn Injector,
) -> Result<(), Error> {
    let input_device = injector.make_touchscreen_device(width, height)?;
    let multi_finger_tap_duration = duration / tap_event_count as u32;

    repeat_with_delay(
        tap_event_count,
        multi_finger_tap_duration,
        |_| {
            // Touch down.
            input_device.multi_finger_tap(Some(fingers.clone()), nanos_from_epoch()?)
        },
        |_| {
            // Touch up.
            input_device.multi_finger_tap(None, nanos_from_epoch()?)
        },
    )
}

pub(crate) fn swipe(
    x0: u32,
    y0: u32,
    x1: u32,
    y1: u32,
    width: u32,
    height: u32,
    move_event_count: usize,
    duration: Duration,
    injector: &mut dyn Injector,
) -> Result<(), Error> {
    multi_finger_swipe(
        vec![(x0, y0)],
        vec![(x1, y1)],
        width,
        height,
        move_event_count,
        duration,
        injector,
    )
}

pub(crate) fn multi_finger_swipe(
    start_fingers: Vec<(u32, u32)>,
    end_fingers: Vec<(u32, u32)>,
    width: u32,
    height: u32,
    move_event_count: usize,
    duration: Duration,
    injector: &mut dyn Injector,
) -> Result<(), Error> {
    ensure!(
        start_fingers.len() == end_fingers.len(),
        "start_fingers.len() != end_fingers.len() ({} != {})",
        start_fingers.len(),
        end_fingers.len()
    );
    ensure!(
        u32::try_from(start_fingers.len() + 1).is_ok(),
        "fingers exceed capacity of `finger_id`!"
    );

    let input_device = injector.make_touchscreen_device(width, height)?;

    // Note: coordinates are coverted to `f64` before subtraction, because u32 subtraction
    // would overflow when swiping from higher coordinates to lower coordinates.
    let finger_delta_x = start_fingers
        .iter()
        .zip(end_fingers.iter())
        .map(|((start_x, _start_y), (end_x, _end_y))| {
            (*end_x as f64 - *start_x as f64) / std::cmp::max(move_event_count, 1) as f64
        })
        .collect::<Vec<_>>();
    let finger_delta_y = start_fingers
        .iter()
        .zip(end_fingers.iter())
        .map(|((_start_x, start_y), (_end_x, end_y))| {
            (*end_y as f64 - *start_y as f64) / std::cmp::max(move_event_count, 1) as f64
        })
        .collect::<Vec<_>>();

    let swipe_event_delay = if move_event_count > 1 {
        // We have move_event_count + 2 events:
        //   DOWN
        //   MOVE x move_event_count
        //   UP
        // so we need (move_event_count + 1) delays.
        duration / (move_event_count + 1) as u32
    } else {
        duration
    };

    repeat_with_delay(
        move_event_count + 2, // +2 to account for DOWN and UP events
        swipe_event_delay,
        |i| {
            let time = nanos_from_epoch()?;
            match i {
                // DOWN
                0 => input_device.multi_finger_tap(
                    Some(
                        start_fingers
                            .iter()
                            .enumerate()
                            .map(|(finger_index, (x, y))| Touch {
                                finger_id: (finger_index + 1) as u32,
                                x: *x as i32,
                                y: *y as i32,
                                width: 0,
                                height: 0,
                            })
                            .collect(),
                    ),
                    time,
                ),
                // MOVE
                i if i <= move_event_count => input_device.multi_finger_tap(
                    Some(
                        start_fingers
                            .iter()
                            .enumerate()
                            .map(|(finger_index, (x, y))| Touch {
                                finger_id: (finger_index + 1) as u32,
                                x: (*x as f64 + (i as f64 * finger_delta_x[finger_index]).round())
                                    as i32,
                                y: (*y as f64 + (i as f64 * finger_delta_y[finger_index]).round())
                                    as i32,
                                width: 0,
                                height: 0,
                            })
                            .collect(),
                    ),
                    time,
                ),
                // UP
                i if i == (move_event_count + 1) => input_device.multi_finger_tap(None, time),
                i => panic!("unexpected loop iteration {}", i),
            }
        },
        |_| Ok(()),
    )
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    mod event_synthesis {
        use {
            super::*,
            fidl::endpoints,
            fidl_fuchsia_ui_input::{
                InputDeviceMarker, InputDeviceProxy as FidlInputDeviceProxy, InputDeviceRequest,
                InputDeviceRequestStream, InputReport, KeyboardReport, MediaButtonsReport,
                TouchscreenReport,
            },
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

        // An `impl Injector` which provides access to the `InputDeviceRequest`s sent to
        // the device registered with the `Injector`. Assumes that only one device is
        // registered.
        struct FakeInjector {
            event_stream: Option<InputDeviceRequestStream>,
        }

        impl Injector for FakeInjector {
            fn make_touchscreen_device(
                &mut self,
                _width: u32,
                _height: u32,
            ) -> Result<Box<dyn InputDevice>, Error> {
                self.make_device()
            }

            fn make_keyboard_device(&mut self) -> Result<Box<dyn InputDevice>, Error> {
                self.make_device()
            }

            fn make_media_buttons_device(&mut self) -> Result<Box<dyn InputDevice>, Error> {
                self.make_device()
            }
        }

        impl FakeInjector {
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
                        vec![Err(format!("called get_events() on Injector with no `event_stream`"))]
                    }
                }
            }

            fn make_device(&mut self) -> Result<Box<dyn InputDevice>, Error> {
                let (proxy, event_stream) =
                    endpoints::create_proxy_and_stream::<InputDeviceMarker>()?;
                self.event_stream = Some(event_stream);
                Ok(Box::new(FakeInputDevice::new(proxy)))
            }
        }

        // Provides an `impl InputDevice` which forwards requests to a `FidlInputDeviceProxy`.
        // Useful when a test wants to inspect the requests to an `InputDevice`.
        struct FakeInputDevice {
            fidl_proxy: FidlInputDeviceProxy,
        }

        impl InputDevice for FakeInputDevice {
            fn media_buttons(
                &self,
                volume_up: bool,
                volume_down: bool,
                mic_mute: bool,
                reset: bool,
                pause: bool,
                time: u64,
            ) -> Result<(), Error> {
                self.fidl_proxy
                    .dispatch_report(&mut InputReport {
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
                    })
                    .map_err(Into::into)
            }

            fn key_press(&self, keyboard: KeyboardReport, time: u64) -> Result<(), Error> {
                self.fidl_proxy
                    .dispatch_report(&mut InputReport {
                        event_time: time,
                        keyboard: Some(Box::new(keyboard)),
                        media_buttons: None,
                        mouse: None,
                        stylus: None,
                        touchscreen: None,
                        sensor: None,
                        trace_id: 0,
                    })
                    .map_err(Into::into)
            }

            fn key_press_usage(&self, usage: Option<u32>, time: u64) -> Result<(), Error> {
                self.key_press(
                    KeyboardReport {
                        pressed_keys: match usage {
                            Some(usage) => vec![usage],
                            None => vec![],
                        },
                    },
                    time,
                )
                .map_err(Into::into)
            }

            fn tap(&self, pos: Option<(u32, u32)>, time: u64) -> Result<(), Error> {
                match pos {
                    Some((x, y)) => self.multi_finger_tap(
                        Some(vec![Touch {
                            finger_id: 1,
                            x: x as i32,
                            y: y as i32,
                            width: 0,
                            height: 0,
                        }]),
                        time,
                    ),
                    None => self.multi_finger_tap(None, time),
                }
                .map_err(Into::into)
            }

            fn multi_finger_tap(
                &self,
                fingers: Option<Vec<Touch>>,
                time: u64,
            ) -> Result<(), Error> {
                self.fidl_proxy
                    .dispatch_report(&mut InputReport {
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
                    })
                    .map_err(Into::into)
            }
        }

        impl FakeInputDevice {
            fn new(fidl_proxy: FidlInputDeviceProxy) -> Self {
                Self { fidl_proxy }
            }
        }

        /// Transforms an `IntoIterator<Item = Result<InlineInputReport, _>>` into a
        /// `Vec<Result</* $field-specific-type */, _>>`, by projecting `$field` out of the
        /// `InlineInputReport`s.
        macro_rules! project {
            ( $events:expr, $field:ident ) => {
                $events
                    .into_iter()
                    .map(|result| result.map(|report| report.$field))
                    .collect::<Vec<_>>()
            };
        }

        #[fasync::run_singlethreaded(test)]
        async fn media_event_report() -> Result<(), Error> {
            let mut fake_event_listener = FakeInjector::new();
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
            let mut fake_event_listener = FakeInjector::new();
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
            let mut fake_event_listener = FakeInjector::new();
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
            let mut fake_event_listener = FakeInjector::new();
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
            let mut fake_event_listener = FakeInjector::new();
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
            let mut fake_event_listener = FakeInjector::new();
            swipe(
                10,
                10,
                100,
                100,
                1000,
                1000,
                2,
                Duration::from_millis(0),
                &mut fake_event_listener,
            )?;
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
            let mut fake_event_listener = FakeInjector::new();
            swipe(
                100,
                100,
                10,
                10,
                1000,
                1000,
                2,
                Duration::from_millis(0),
                &mut fake_event_listener,
            )?;
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

        #[fasync::run_singlethreaded(test)]
        async fn multi_finger_swipe_event_report() -> Result<(), Error> {
            let mut fake_event_listener = FakeInjector::new();
            multi_finger_swipe(
                vec![(10, 10), (20, 20), (30, 30)],
                vec![(100, 100), (120, 120), (150, 150)],
                1000,
                1000,
                2,
                Duration::from_millis(0),
                &mut fake_event_listener,
            )?;
            assert_eq!(
                project!(fake_event_listener.get_events().await, touchscreen),
                [
                    Ok(Some(TouchscreenReport {
                        touches: vec![
                            Touch { finger_id: 1, x: 10, y: 10, width: 0, height: 0 },
                            Touch { finger_id: 2, x: 20, y: 20, width: 0, height: 0 },
                            Touch { finger_id: 3, x: 30, y: 30, width: 0, height: 0 }
                        ],
                    })),
                    Ok(Some(TouchscreenReport {
                        touches: vec![
                            Touch { finger_id: 1, x: 55, y: 55, width: 0, height: 0 },
                            Touch { finger_id: 2, x: 70, y: 70, width: 0, height: 0 },
                            Touch { finger_id: 3, x: 90, y: 90, width: 0, height: 0 }
                        ],
                    })),
                    Ok(Some(TouchscreenReport {
                        touches: vec![
                            Touch { finger_id: 1, x: 100, y: 100, width: 0, height: 0 },
                            Touch { finger_id: 2, x: 120, y: 120, width: 0, height: 0 },
                            Touch { finger_id: 3, x: 150, y: 150, width: 0, height: 0 }
                        ],
                    })),
                    Ok(Some(TouchscreenReport { touches: vec![] })),
                ]
            );
            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn multi_finger_swipe_event_report_inverted() -> Result<(), Error> {
            let mut fake_event_listener = FakeInjector::new();
            multi_finger_swipe(
                vec![(100, 100), (120, 120), (150, 150)],
                vec![(10, 10), (20, 20), (30, 30)],
                1000,
                1000,
                2,
                Duration::from_millis(0),
                &mut fake_event_listener,
            )?;
            assert_eq!(
                project!(fake_event_listener.get_events().await, touchscreen),
                [
                    Ok(Some(TouchscreenReport {
                        touches: vec![
                            Touch { finger_id: 1, x: 100, y: 100, width: 0, height: 0 },
                            Touch { finger_id: 2, x: 120, y: 120, width: 0, height: 0 },
                            Touch { finger_id: 3, x: 150, y: 150, width: 0, height: 0 }
                        ],
                    })),
                    Ok(Some(TouchscreenReport {
                        touches: vec![
                            Touch { finger_id: 1, x: 55, y: 55, width: 0, height: 0 },
                            Touch { finger_id: 2, x: 70, y: 70, width: 0, height: 0 },
                            Touch { finger_id: 3, x: 90, y: 90, width: 0, height: 0 }
                        ],
                    })),
                    Ok(Some(TouchscreenReport {
                        touches: vec![
                            Touch { finger_id: 1, x: 10, y: 10, width: 0, height: 0 },
                            Touch { finger_id: 2, x: 20, y: 20, width: 0, height: 0 },
                            Touch { finger_id: 3, x: 30, y: 30, width: 0, height: 0 }
                        ],
                    })),
                    Ok(Some(TouchscreenReport { touches: vec![] })),
                ]
            );
            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn multi_finger_swipe_event_zero_move_events() -> Result<(), Error> {
            let mut fake_event_listener = FakeInjector::new();
            multi_finger_swipe(
                vec![(10, 10), (20, 20), (30, 30)],
                vec![(100, 100), (120, 120), (150, 150)],
                1000,
                1000,
                0,
                Duration::from_millis(0),
                &mut fake_event_listener,
            )?;
            assert_eq!(
                project!(fake_event_listener.get_events().await, touchscreen),
                [
                    Ok(Some(TouchscreenReport {
                        touches: vec![
                            Touch { finger_id: 1, x: 10, y: 10, width: 0, height: 0 },
                            Touch { finger_id: 2, x: 20, y: 20, width: 0, height: 0 },
                            Touch { finger_id: 3, x: 30, y: 30, width: 0, height: 0 }
                        ],
                    })),
                    Ok(Some(TouchscreenReport { touches: vec![] })),
                ]
            );
            Ok(())
        }
    }

    mod device_registration {
        use {super::*, matches::assert_matches};

        #[derive(Debug)]
        enum DeviceType {
            Keyboard,
            MediaButtons,
            Touchscreen,
        }

        // An `impl Injector` which provides access to the `DeviceType`s which have been
        // registered with the `Injector`.
        struct FakeInjector {
            device_types: Vec<DeviceType>,
        }

        impl Injector for FakeInjector {
            fn make_touchscreen_device(
                &mut self,
                _width: u32,
                _height: u32,
            ) -> Result<Box<dyn InputDevice>, Error> {
                self.make_device(DeviceType::Touchscreen)
            }

            fn make_keyboard_device(&mut self) -> Result<Box<dyn InputDevice>, Error> {
                self.make_device(DeviceType::Keyboard)
            }

            fn make_media_buttons_device(&mut self) -> Result<Box<dyn InputDevice>, Error> {
                self.make_device(DeviceType::MediaButtons)
            }
        }

        impl FakeInjector {
            fn new() -> Self {
                Self { device_types: vec![] }
            }

            fn make_device(
                &mut self,
                device_type: DeviceType,
            ) -> Result<Box<dyn InputDevice>, Error> {
                self.device_types.push(device_type);
                Ok(Box::new(FakeInputDevice))
            }
        }

        // Provides an `impl InputDevice` which always returns `Ok(())`. Useful when the
        // events themselves are not important to the test.
        struct FakeInputDevice;

        impl InputDevice for FakeInputDevice {
            fn media_buttons(
                &self,
                _volume_up: bool,
                _volume_down: bool,
                _mic_mute: bool,
                _reset: bool,
                _pause: bool,
                _time: u64,
            ) -> Result<(), Error> {
                Ok(())
            }

            fn key_press(&self, _keyboard: KeyboardReport, _time: u64) -> Result<(), Error> {
                Ok(())
            }

            fn key_press_usage(&self, _usage: Option<u32>, _time: u64) -> Result<(), Error> {
                Ok(())
            }

            fn tap(&self, _pos: Option<(u32, u32)>, _time: u64) -> Result<(), Error> {
                Ok(())
            }

            fn multi_finger_tap(
                &self,
                _fingers: Option<Vec<Touch>>,
                _time: u64,
            ) -> Result<(), Error> {
                Ok(())
            }
        }

        #[fasync::run_until_stalled(test)]
        async fn media_button_event_registers_media_buttons_device() -> Result<(), Error> {
            let mut injector = FakeInjector::new();
            media_button_event(false, false, false, false, false, &mut injector)?;
            assert_matches!(injector.device_types.as_slice(), [DeviceType::MediaButtons]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn keyboard_event_registers_keyboard() -> Result<(), Error> {
            let mut injector = FakeInjector::new();
            keyboard_event(40, Duration::from_millis(0), &mut injector)?;
            assert_matches!(injector.device_types.as_slice(), [DeviceType::Keyboard]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn text_event_registers_keyboard() -> Result<(), Error> {
            let mut injector = FakeInjector::new();
            text("A".to_string(), Duration::from_millis(0), &mut injector)?;
            assert_matches!(injector.device_types.as_slice(), [DeviceType::Keyboard]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn multi_finger_tap_event_registers_touchscreen() -> Result<(), Error> {
            let mut injector = FakeInjector::new();
            multi_finger_tap_event(vec![], 1000, 1000, 1, Duration::from_millis(0), &mut injector)?;
            assert_matches!(injector.device_types.as_slice(), [DeviceType::Touchscreen]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn tap_event_registers_touchscreen() -> Result<(), Error> {
            let mut injector = FakeInjector::new();
            tap_event(0, 0, 1000, 1000, 1, Duration::from_millis(0), &mut injector)?;
            assert_matches!(injector.device_types.as_slice(), [DeviceType::Touchscreen]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn swipe_registers_touchscreen() -> Result<(), Error> {
            let mut injector = FakeInjector::new();
            swipe(0, 0, 1, 1, 1000, 1000, 1, Duration::from_millis(0), &mut injector)?;
            assert_matches!(injector.device_types.as_slice(), [DeviceType::Touchscreen]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn multi_finger_swipe_registers_touchscreen() -> Result<(), Error> {
            let mut injector = FakeInjector::new();
            multi_finger_swipe(
                vec![],
                vec![],
                1000,
                1000,
                1,
                Duration::from_millis(0),
                &mut injector,
            )?;
            assert_matches!(injector.device_types.as_slice(), [DeviceType::Touchscreen]);
            Ok(())
        }
    }
}
