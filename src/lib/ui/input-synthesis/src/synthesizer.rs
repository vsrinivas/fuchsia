// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{inverse_keymap::InverseKeymap, keymaps},
    anyhow::{ensure, Error},
    fidl_fuchsia_ui_input::{self, KeyboardReport, Touch},
    fuchsia_zircon as zx,
    std::{convert::TryFrom, thread, time::Duration},
};

// Abstracts over input injection services (which are provided by input device registries).
pub trait InputDeviceRegistry {
    fn add_touchscreen_device(
        &mut self,
        width: u32,
        height: u32,
    ) -> Result<Box<dyn InputDevice>, Error>;
    fn add_keyboard_device(&mut self) -> Result<Box<dyn InputDevice>, Error>;
    fn add_media_buttons_device(&mut self) -> Result<Box<dyn InputDevice>, Error>;
}

// Abstracts over the various interactions that a user might have with an input device.
// Note that the input-synthesis crate deliberately chooses not to "sub-type" input devices.
// This avoids additional code complexity, and allows the crate to support tests that
// deliberately send events that do not match the expected event type for a device.
pub trait InputDevice {
    fn media_buttons(
        &mut self,
        volume_up: bool,
        volume_down: bool,
        mic_mute: bool,
        reset: bool,
        pause: bool,
        camera_disable: bool,
        time: u64,
    ) -> Result<(), Error>;
    fn key_press(&mut self, keyboard: KeyboardReport, time: u64) -> Result<(), Error>;
    fn key_press_usage(&mut self, usage: Option<u32>, time: u64) -> Result<(), Error>;
    fn tap(&mut self, pos: Option<(u32, u32)>, time: u64) -> Result<(), Error>;
    fn multi_finger_tap(&mut self, fingers: Option<Vec<Touch>>, time: u64) -> Result<(), Error>;
}

fn monotonic_nanos() -> Result<u64, Error> {
    u64::try_from(zx::Time::get_monotonic().into_nanos()).map_err(Into::into)
}

fn repeat_with_delay(
    times: usize,
    delay: Duration,
    device: &mut dyn InputDevice,
    f1: impl Fn(usize, &mut dyn InputDevice) -> Result<(), Error>,
    f2: impl Fn(usize, &mut dyn InputDevice) -> Result<(), Error>,
) -> Result<(), Error> {
    for i in 0..times {
        f1(i, device)?;
        thread::sleep(delay);
        f2(i, device)?;
    }

    Ok(())
}

pub(crate) fn media_button_event(
    volume_up: bool,
    volume_down: bool,
    mic_mute: bool,
    reset: bool,
    pause: bool,
    camera_disable: bool,
    registry: &mut dyn InputDeviceRegistry,
) -> Result<(), Error> {
    let mut input_device = registry.add_media_buttons_device()?;
    input_device.media_buttons(
        volume_up,
        volume_down,
        mic_mute,
        reset,
        pause,
        camera_disable,
        monotonic_nanos()?,
    )
}

pub(crate) fn keyboard_event(
    usage: u32,
    duration: Duration,
    registry: &mut dyn InputDeviceRegistry,
) -> Result<(), Error> {
    let mut input_device = registry.add_keyboard_device()?;

    repeat_with_delay(
        1,
        duration,
        input_device.as_mut(),
        |_i, device| {
            // Key pressed.
            device.key_press_usage(Some(usage), monotonic_nanos()?)
        },
        |_i, device| {
            // Key released.
            device.key_press_usage(None, monotonic_nanos()?)
        },
    )
}

pub(crate) fn text(
    input: String,
    key_event_duration: Duration,
    registry: &mut dyn InputDeviceRegistry,
) -> Result<(), Error> {
    let mut input_device = registry.add_keyboard_device()?;
    let key_sequence = InverseKeymap::new(keymaps::QWERTY_MAP)
        .derive_key_sequence(&input)
        .ok_or_else(|| anyhow::format_err!("Cannot translate text to key sequence"))?;

    let mut key_iter = key_sequence.into_iter().peekable();
    while let Some(keyboard) = key_iter.next() {
        input_device.key_press(keyboard, monotonic_nanos()?)?;
        if key_iter.peek().is_some() {
            thread::sleep(key_event_duration);
        }
    }

    Ok(())
}

pub fn tap_event(
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    tap_event_count: usize,
    duration: Duration,
    registry: &mut dyn InputDeviceRegistry,
) -> Result<(), Error> {
    let mut input_device = registry.add_touchscreen_device(width, height)?;
    let tap_duration = duration / tap_event_count as u32;

    repeat_with_delay(
        tap_event_count,
        tap_duration,
        input_device.as_mut(),
        |_i, device| {
            // Touch down.
            device.tap(Some((x, y)), monotonic_nanos()?)
        },
        |_i, device| {
            // Touch up.
            device.tap(None, monotonic_nanos()?)
        },
    )
}

pub(crate) fn multi_finger_tap_event(
    fingers: Vec<Touch>,
    width: u32,
    height: u32,
    tap_event_count: usize,
    duration: Duration,
    registry: &mut dyn InputDeviceRegistry,
) -> Result<(), Error> {
    let mut input_device = registry.add_touchscreen_device(width, height)?;
    let multi_finger_tap_duration = duration / tap_event_count as u32;

    repeat_with_delay(
        tap_event_count,
        multi_finger_tap_duration,
        input_device.as_mut(),
        |_i, device| {
            // Touch down.
            device.multi_finger_tap(Some(fingers.clone()), monotonic_nanos()?)
        },
        |_i, device| {
            // Touch up.
            device.multi_finger_tap(None, monotonic_nanos()?)
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
    registry: &mut dyn InputDeviceRegistry,
) -> Result<(), Error> {
    multi_finger_swipe(
        vec![(x0, y0)],
        vec![(x1, y1)],
        width,
        height,
        move_event_count,
        duration,
        registry,
    )
}

pub(crate) fn multi_finger_swipe(
    start_fingers: Vec<(u32, u32)>,
    end_fingers: Vec<(u32, u32)>,
    width: u32,
    height: u32,
    move_event_count: usize,
    duration: Duration,
    registry: &mut dyn InputDeviceRegistry,
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

    let mut input_device = registry.add_touchscreen_device(width, height)?;

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
        input_device.as_mut(),
        |i, device| {
            let time = monotonic_nanos()?;
            match i {
                // DOWN
                0 => device.multi_finger_tap(
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
                i if i <= move_event_count => device.multi_finger_tap(
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
                i if i == (move_event_count + 1) => device.multi_finger_tap(None, time),
                i => panic!("unexpected loop iteration {}", i),
            }
        },
        |_, _| Ok(()),
    )
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    mod event_synthesis {
        use {
            super::*,
            anyhow::Context as _,
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
            event_time: u64,
            keyboard: Option<KeyboardReport>,
            media_buttons: Option<MediaButtonsReport>,
            touchscreen: Option<TouchscreenReport>,
        }

        impl InlineInputReport {
            fn new(input_report: InputReport) -> Self {
                Self {
                    event_time: input_report.event_time,
                    keyboard: input_report.keyboard.map(|boxed| *boxed),
                    media_buttons: input_report.media_buttons.map(|boxed| *boxed),
                    touchscreen: input_report.touchscreen.map(|boxed| *boxed),
                }
            }
        }

        // An `impl InputDeviceRegistry` which provides access to the `InputDeviceRequest`s sent to
        // the device registered with the `InputDeviceRegistry`. Assumes that only one device is
        // registered.
        struct FakeInputDeviceRegistry {
            event_stream: Option<InputDeviceRequestStream>,
        }

        impl InputDeviceRegistry for FakeInputDeviceRegistry {
            fn add_touchscreen_device(
                &mut self,
                _width: u32,
                _height: u32,
            ) -> Result<Box<dyn InputDevice>, Error> {
                self.add_device()
            }

            fn add_keyboard_device(&mut self) -> Result<Box<dyn InputDevice>, Error> {
                self.add_device()
            }

            fn add_media_buttons_device(&mut self) -> Result<Box<dyn InputDevice>, Error> {
                self.add_device()
            }
        }

        impl FakeInputDeviceRegistry {
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
                    None => vec![Err(format!(
                        "called get_events() on InputDeviceRegistry with no `event_stream`"
                    ))],
                }
            }

            fn add_device(&mut self) -> Result<Box<dyn InputDevice>, Error> {
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
                &mut self,
                volume_up: bool,
                volume_down: bool,
                mic_mute: bool,
                reset: bool,
                pause: bool,
                camera_disable: bool,
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
                            camera_disable,
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

            fn key_press(&mut self, keyboard: KeyboardReport, time: u64) -> Result<(), Error> {
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

            fn key_press_usage(&mut self, usage: Option<u32>, time: u64) -> Result<(), Error> {
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

            fn tap(&mut self, pos: Option<(u32, u32)>, time: u64) -> Result<(), Error> {
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
                &mut self,
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
            let mut fake_event_listener = FakeInputDeviceRegistry::new();
            media_button_event(true, false, true, false, true, true, &mut fake_event_listener)?;
            assert_eq!(
                project!(fake_event_listener.get_events().await, media_buttons),
                [Ok(Some(MediaButtonsReport {
                    volume_up: true,
                    volume_down: false,
                    mic_mute: true,
                    reset: false,
                    pause: true,
                    camera_disable: true,
                }))]
            );
            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn keyboard_event_report() -> Result<(), Error> {
            let mut fake_event_listener = FakeInputDeviceRegistry::new();
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
            let mut fake_event_listener = FakeInputDeviceRegistry::new();
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
            let mut fake_event_listener = FakeInputDeviceRegistry::new();
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
            let mut fake_event_listener = FakeInputDeviceRegistry::new();
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
            let mut fake_event_listener = FakeInputDeviceRegistry::new();
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
            let mut fake_event_listener = FakeInputDeviceRegistry::new();
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
            let mut fake_event_listener = FakeInputDeviceRegistry::new();
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
            let mut fake_event_listener = FakeInputDeviceRegistry::new();
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
            let mut fake_event_listener = FakeInputDeviceRegistry::new();
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

        #[fasync::run_singlethreaded(test)]
        async fn events_use_monotonic_time() -> Result<(), Error> {
            let mut fake_event_listener = FakeInputDeviceRegistry::new();
            let synthesis_start_time = monotonic_nanos()?;
            media_button_event(true, false, true, false, true, true, &mut fake_event_listener)?;

            let synthesis_end_time = monotonic_nanos()?;
            let fidl_result = fake_event_listener
                .get_events()
                .await
                .into_iter()
                .nth(0)
                .expect("received 0 events");
            let timestamp =
                fidl_result.map_err(anyhow::Error::msg).context("fidl call")?.event_time;

            // Note well: neither condition is sufficient on its own, to verify that
            // `synthesizer` has used the correct clock. For example:
            //
            // * `timestamp >= synthesis_start_time` would be true for a `UNIX_EPOCH` clock
            //   with the correct time, since the elapsed time from 1970-01-01T00:00:00+00:00
            //   to now is (much) larger than the elapsed time from boot to `synthesis_start_time`
            // * `timestamp <= synthesis_end_time` would be true for a `UNIX_EPOCH` clock
            //   has been recently set to 0, because `synthesis_end_time` is highly unlikely to
            //   be near 0 (as it is monotonic from boot)
            //
            // By bracketing between monotonic clock reads before and after the event generation,
            // this test avoids the hazards above. The test also avoids the hazard of using a
            // fixed offset from the start time (which could flake on a slow builder).
            assert!(
                timestamp >= synthesis_start_time,
                "timestamp={} should be >= start={}",
                timestamp,
                synthesis_start_time
            );
            assert!(
                timestamp <= synthesis_end_time,
                "timestamp={} should be <= end={}",
                timestamp,
                synthesis_end_time
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

        // An `impl InputDeviceRegistry` which provides access to the `DeviceType`s which have been
        // registered with the `InputDeviceRegistry`.
        struct FakeInputDeviceRegistry {
            device_types: Vec<DeviceType>,
        }

        impl InputDeviceRegistry for FakeInputDeviceRegistry {
            fn add_touchscreen_device(
                &mut self,
                _width: u32,
                _height: u32,
            ) -> Result<Box<dyn InputDevice>, Error> {
                self.add_device(DeviceType::Touchscreen)
            }

            fn add_keyboard_device(&mut self) -> Result<Box<dyn InputDevice>, Error> {
                self.add_device(DeviceType::Keyboard)
            }

            fn add_media_buttons_device(&mut self) -> Result<Box<dyn InputDevice>, Error> {
                self.add_device(DeviceType::MediaButtons)
            }
        }

        impl FakeInputDeviceRegistry {
            fn new() -> Self {
                Self { device_types: vec![] }
            }

            fn add_device(
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
                &mut self,
                _volume_up: bool,
                _volume_down: bool,
                _mic_mute: bool,
                _reset: bool,
                _pause: bool,
                _camera_disable: bool,
                _time: u64,
            ) -> Result<(), Error> {
                Ok(())
            }

            fn key_press(&mut self, _keyboard: KeyboardReport, _time: u64) -> Result<(), Error> {
                Ok(())
            }

            fn key_press_usage(&mut self, _usage: Option<u32>, _time: u64) -> Result<(), Error> {
                Ok(())
            }

            fn tap(&mut self, _pos: Option<(u32, u32)>, _time: u64) -> Result<(), Error> {
                Ok(())
            }

            fn multi_finger_tap(
                &mut self,
                _fingers: Option<Vec<Touch>>,
                _time: u64,
            ) -> Result<(), Error> {
                Ok(())
            }
        }

        #[fasync::run_until_stalled(test)]
        async fn media_button_event_registers_media_buttons_device() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            media_button_event(false, false, false, false, false, false, &mut registry)?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::MediaButtons]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn keyboard_event_registers_keyboard() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            keyboard_event(40, Duration::from_millis(0), &mut registry)?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::Keyboard]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn text_event_registers_keyboard() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            text("A".to_string(), Duration::from_millis(0), &mut registry)?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::Keyboard]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn multi_finger_tap_event_registers_touchscreen() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            multi_finger_tap_event(vec![], 1000, 1000, 1, Duration::from_millis(0), &mut registry)?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::Touchscreen]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn tap_event_registers_touchscreen() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            tap_event(0, 0, 1000, 1000, 1, Duration::from_millis(0), &mut registry)?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::Touchscreen]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn swipe_registers_touchscreen() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            swipe(0, 0, 1, 1, 1000, 1000, 1, Duration::from_millis(0), &mut registry)?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::Touchscreen]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn multi_finger_swipe_registers_touchscreen() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            multi_finger_swipe(
                vec![],
                vec![],
                1000,
                1000,
                1,
                Duration::from_millis(0),
                &mut registry,
            )?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::Touchscreen]);
            Ok(())
        }
    }
}
