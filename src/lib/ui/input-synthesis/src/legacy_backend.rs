// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::synthesizer,
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_ui_input::{
        self, DeviceDescriptor, InputDeviceMarker, InputDeviceRegistryMarker, InputReport,
        KeyboardReport, MediaButtonsReport, Touch, TouchscreenReport,
    },
    fuchsia_component as app,
};

// Provides a handle to an `impl synthesizer::Injector`, which works with input
// pipelines that support the (legacy) `fuchsia.ui.input.InputDeviceRegistry` protocol.
pub(crate) struct Injector;

impl synthesizer::Injector for Injector {
    fn register_device(
        &mut self,
        device: &mut DeviceDescriptor,
        server: ServerEnd<InputDeviceMarker>,
    ) -> Result<(), Error> {
        let registry = app::client::connect_to_service::<InputDeviceRegistryMarker>()?;
        registry.register_device(device, server)?;
        Ok(())
    }
}

pub(crate) fn media_buttons(
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

pub(crate) fn key_press(keyboard: KeyboardReport, time: u64) -> InputReport {
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

pub(crate) fn key_press_usage(usage: Option<u32>, time: u64) -> InputReport {
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

pub(crate) fn tap(pos: Option<(u32, u32)>, time: u64) -> InputReport {
    match pos {
        Some((x, y)) => multi_finger_tap(
            Some(vec![Touch { finger_id: 1, x: x as i32, y: y as i32, width: 0, height: 0 }]),
            time,
        ),
        None => multi_finger_tap(None, time),
    }
}

pub(crate) fn multi_finger_tap(fingers: Option<Vec<Touch>>, time: u64) -> InputReport {
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

#[cfg(test)]
mod tests {
    use {super::*, proptest::prelude::*};

    proptest! {
        #[test]
        fn media_buttons_populates_report_correctly(
            volume_up: bool,
            volume_down: bool,
            mic_mute: bool,
            reset: bool,
            pause: bool,
            event_time: u64
        ) {
            prop_assert_eq!(
                media_buttons(volume_up, volume_down, mic_mute, reset, pause, event_time),
                InputReport {
                    event_time,
                    keyboard: None,
                    media_buttons: Some(Box::new(MediaButtonsReport {
                        volume_up,
                        volume_down,
                        mic_mute,
                        reset,
                        pause
                    })),
                    mouse: None,
                    stylus: None,
                    touchscreen: None,
                    sensor: None,
                    trace_id: 0
                }
            );
        }
    }

    #[test]
    fn key_press_populates_report_correctly() {
        assert_eq!(
            key_press(KeyboardReport { pressed_keys: vec![1, 2, 3] }, 200),
            InputReport {
                event_time: 200,
                keyboard: Some(Box::new(KeyboardReport { pressed_keys: vec![1, 2, 3] })),
                media_buttons: None,
                mouse: None,
                stylus: None,
                touchscreen: None,
                sensor: None,
                trace_id: 0
            }
        );
    }

    #[test]
    fn key_press_usage_populates_report_correctly_when_a_key_is_pressed() {
        assert_eq!(
            key_press_usage(Some(1), 300),
            InputReport {
                event_time: 300,
                keyboard: Some(Box::new(KeyboardReport { pressed_keys: vec![1] })),
                media_buttons: None,
                mouse: None,
                stylus: None,
                touchscreen: None,
                sensor: None,
                trace_id: 0
            }
        );
    }

    #[test]
    fn key_press_usage_populates_report_correctly_when_no_key_is_pressed() {
        assert_eq!(
            key_press_usage(None, 400),
            InputReport {
                event_time: 400,
                keyboard: Some(Box::new(KeyboardReport { pressed_keys: vec![] })),
                media_buttons: None,
                mouse: None,
                stylus: None,
                touchscreen: None,
                sensor: None,
                trace_id: 0
            }
        );
    }

    #[test]
    fn tap_populates_report_correctly_when_finger_is_present() {
        assert_eq!(
            tap(Some((10, 20)), 500),
            InputReport {
                event_time: 500,
                keyboard: None,
                media_buttons: None,
                mouse: None,
                stylus: None,
                touchscreen: Some(Box::new(TouchscreenReport {
                    touches: vec![Touch { finger_id: 1, x: 10, y: 20, width: 0, height: 0 },]
                })),
                sensor: None,
                trace_id: 0
            }
        );
    }

    #[test]
    fn tap_populates_report_correctly_when_finger_is_absent() {
        assert_eq!(
            tap(None, 600),
            InputReport {
                event_time: 600,
                keyboard: None,
                media_buttons: None,
                mouse: None,
                stylus: None,
                touchscreen: Some(Box::new(TouchscreenReport { touches: vec![] })),
                sensor: None,
                trace_id: 0
            }
        );
    }

    #[test]
    fn multi_finger_tap_populates_report_correctly_when_fingers_are_present() {
        assert_eq!(
            multi_finger_tap(
                Some(vec![
                    Touch { finger_id: 1, x: 99, y: 100, width: 10, height: 20 },
                    Touch { finger_id: 2, x: 199, y: 201, width: 30, height: 40 }
                ]),
                700
            ),
            InputReport {
                event_time: 700,
                keyboard: None,
                media_buttons: None,
                mouse: None,
                stylus: None,
                touchscreen: Some(Box::new(TouchscreenReport {
                    touches: vec![
                        Touch { finger_id: 1, x: 99, y: 100, width: 10, height: 20 },
                        Touch { finger_id: 2, x: 199, y: 201, width: 30, height: 40 }
                    ]
                })),
                sensor: None,
                trace_id: 0
            }
        );
    }

    #[test]
    fn multi_finger_tap_populates_report_correctly_when_no_fingers_are_present() {
        assert_eq!(
            multi_finger_tap(None, 800),
            InputReport {
                event_time: 800,
                keyboard: None,
                media_buttons: None,
                mouse: None,
                stylus: None,
                touchscreen: Some(Box::new(TouchscreenReport { touches: vec![] })),
                sensor: None,
                trace_id: 0
            }
        );
    }
}
