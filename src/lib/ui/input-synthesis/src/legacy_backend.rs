// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{synthesizer, usages::Usages},
    anyhow::Error,
    fidl::endpoints,
    fidl_fuchsia_ui_input::{
        self, Axis, AxisScale, DeviceDescriptor, InputDeviceMarker, InputDeviceProxy,
        InputDeviceRegistryMarker, InputReport, KeyboardDescriptor, KeyboardReport,
        MediaButtonsDescriptor, MediaButtonsReport, Range, Touch, TouchscreenDescriptor,
        TouchscreenReport,
    },
    fuchsia_component as app,
};

// Provides a handle to an `impl synthesizer::Injector`, which works with input
// pipelines that support the (legacy) `fuchsia.ui.input.InputDeviceRegistry` protocol.
pub(crate) struct Injector;

// Wraps `DeviceDescriptor` FIDL table fields for descriptors into a single Rust type,
// allowing us to pass any of them to `self.register_device()`.
#[derive(Debug)]
enum UniformDeviceDescriptor {
    Keyboard(KeyboardDescriptor),
    MediaButtons(MediaButtonsDescriptor),
    Touchscreen(TouchscreenDescriptor),
}

impl synthesizer::Injector for self::Injector {
    fn make_touchscreen_device(
        &mut self,
        width: u32,
        height: u32,
    ) -> Result<InputDeviceProxy, Error> {
        self.register_device(UniformDeviceDescriptor::Touchscreen(TouchscreenDescriptor {
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
        }))
    }

    fn make_keyboard_device(&mut self) -> Result<InputDeviceProxy, Error> {
        self.register_device(UniformDeviceDescriptor::Keyboard(KeyboardDescriptor {
            keys: (Usages::HidUsageKeyA as u32..Usages::HidUsageKeyRightGui as u32).collect(),
        }))
    }

    fn make_media_buttons_device(&mut self) -> Result<InputDeviceProxy, Error> {
        self.register_device(UniformDeviceDescriptor::MediaButtons(MediaButtonsDescriptor {
            buttons: fidl_fuchsia_ui_input::MIC_MUTE
                | fidl_fuchsia_ui_input::VOLUME_DOWN
                | fidl_fuchsia_ui_input::VOLUME_UP,
        }))
    }
}

impl Injector {
    fn register_device(
        &self,
        descriptor: UniformDeviceDescriptor,
    ) -> Result<InputDeviceProxy, Error> {
        let registry = app::client::connect_to_service::<InputDeviceRegistryMarker>()?;
        let mut device = DeviceDescriptor {
            device_info: None,
            keyboard: None,
            media_buttons: None,
            mouse: None,
            stylus: None,
            touchscreen: None,
            sensor: None,
        };

        match descriptor {
            UniformDeviceDescriptor::Keyboard(descriptor) => {
                device.keyboard = Some(Box::new(descriptor))
            }
            UniformDeviceDescriptor::Touchscreen(descriptor) => {
                device.touchscreen = Some(Box::new(descriptor))
            }
            UniformDeviceDescriptor::MediaButtons(descriptor) => {
                device.media_buttons = Some(Box::new(descriptor))
            }
        };

        let (input_device_client, input_device_server) =
            endpoints::create_endpoints::<InputDeviceMarker>()?;
        registry.register_device(&mut device, input_device_server)?;
        Ok(input_device_client.into_proxy()?)
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
