// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{inverse_keymap::InverseKeymap, keymaps},
    anyhow::{ensure, Error},
    async_trait::async_trait,
    fidl_fuchsia_input as input,
    fidl_fuchsia_ui_input::{self, KeyboardReport, Touch},
    fidl_fuchsia_ui_input3 as input3, fuchsia_async as fasync,
    fuchsia_syslog::fx_log_debug,
    fuchsia_zircon as zx,
    serde::{Deserialize, Deserializer},
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
#[async_trait(?Send)]
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

    /// Sends a keyboard report with keys defined mostly in terms of USB HID usage
    /// page 7. This is sufficient for keyboard keys, but does not cover the full
    /// extent of keys that Fuchsia supports. As result, the KeyboardReport is converted
    /// internally into Fuchsia's encoding before being forwarded.
    fn key_press(&mut self, keyboard: KeyboardReport, time: u64) -> Result<(), Error>;

    /// Sends a keyboard report using the whole range of key codes. Key codes provided
    /// are not modified or mapped in any way.
    /// This differs from `key_press`, which performs special mapping for key codes
    /// from USB HID Page 0x7.
    fn key_press_raw(&mut self, keyboard: KeyboardReport, time: u64) -> Result<(), Error>;
    fn key_press_usage(&mut self, usage: Option<u32>, time: u64) -> Result<(), Error>;
    fn tap(&mut self, pos: Option<(u32, u32)>, time: u64) -> Result<(), Error>;
    fn multi_finger_tap(&mut self, fingers: Option<Vec<Touch>>, time: u64) -> Result<(), Error>;

    // Returns a `Future` which resolves when all input reports for this device
    // have been sent to the FIDL peer, or when an error occurs.
    //
    // The possible errors are implementation-specific, but may include:
    // * Errors reading from the FIDL peer
    // * Errors writing to the FIDL peer
    //
    // # Resolves to
    // * `Ok(())` if all reports were written successfully
    // * `Err` otherwise
    //
    // # Note
    // When the future resolves, input reports may still be sitting unread in the
    // channel to the FIDL peer.
    async fn serve_reports(self: Box<Self>) -> Result<(), Error>;
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

pub(crate) async fn media_button_event(
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
    )?;
    input_device.serve_reports().await
}

/// A single key event to be replayed by `dispatch_key_events_async`.
///
/// See [crate::dispatch_key_events] for details of the key event type and the event timing.
///
/// For example, a key press like this:
///
/// ```ignore
/// Key1: _________/"""""""""""""""\\___________
///                ^               ^--- key released
///                `------------------- key pressed
///       |<------>|  <-- duration_since_start (50ms)
///       |<---------------------->| duration_since_start (100ms)
/// ```
///
/// would be described with a sequence of two `TimedKeyEvent`s (pseudo-code):
///
/// ```
/// [
///    { Key1,  50ms, PRESSED  },
///    { Key1, 100ms, RELEASED },
/// ]
/// ```
///
/// This is not overly useful in the case of a single key press, but is useful to model multiple
/// concurrent keypresses, while allowing an arbitrary interleaving of key events.
///
/// Consider a more complicated timing diagram like this one:
///
/// ```ignore
/// Key1: _________/"""""""""""""""\\_____________
/// Key2: ____/"""""""""""""""\\__________________
/// Key3: ______/"""""""""""""""\\________________
/// Key4: _____________/"""""""""""""""\\_________
/// Key5: ________________ __/""""""\\____________
/// Key6: ________/""""""\\_______________________
/// ```
///
/// It then becomes obvious how modeling individual events allows us to express this interaction.
/// Furthermore anchoring `duration_since_start` to the beginning of the key sequence (instead of,
/// for example, specifying the duration of each key press) gives a common time reference and makes
/// it fairly easy to express the intended key interaction in terms of a `TimedKeyEvent` sequence.
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct TimedKeyEvent {
    /// The [input::Key] which changed state.
    pub key: input::Key,
    /// The duration of time,  relative to the start of the key event sequence that this `TimedKeyEvent`
    /// is part of, at which this event happened at.
    pub duration_since_start: Duration,
    /// The type of state change that happened to `key`.  Was it pressed, released or something
    /// else.
    pub event_type: input3::KeyEventType,
}

impl TimedKeyEvent {
    /// Creates a new [TimedKeyEvent] to inject into the input pipeline.  `key` is
    /// the key to be pressed (using Fuchsia HID-like encoding), `type_` is the
    /// event type (Pressed, or Released etc), and `duration_since_start` is the
    /// duration since the start of the entire event sequence that the key event
    /// should be scheduled at.
    pub fn new(
        key: input::Key,
        type_: input3::KeyEventType,
        duration_since_start: Duration,
    ) -> Self {
        Self { key, duration_since_start, event_type: type_ }
    }

    /// Deserializes a vector of `TimedKeyEvent`.
    /// A custom deserializer is used because Vec<_> does not work
    /// with serde, and the [TimedKeyEvent] has constituents that don't
    /// have a derived serde representation.
    /// See: https://github.com/serde-rs/serde/issues/723#issuecomment-382501277
    pub fn vec<'de, D>(deserializer: D) -> Result<Vec<TimedKeyEvent>, D::Error>
    where
        D: Deserializer<'de>,
    {
        // Should correspond to TimedKeyEvent, except all fields are described by their underlying
        // primitive values.
        #[derive(Deserialize, Debug)]
        struct TimedKeyEventDes {
            // The Fuchsia encoded USB HID key, per input::Key.
            key: u32,
            // A Duration.
            duration_millis: u64,
            // An input3::TimedKeyEventType.
            #[serde(rename = "type")]
            type_: u32,
        }

        impl Into<TimedKeyEvent> for TimedKeyEventDes {
            /// Reconstructs the typed elements of [TimedKeyEvent] from primitives.
            fn into(self) -> TimedKeyEvent {
                TimedKeyEvent::new(
                    input::Key::from_primitive(self.key)
                        .expect(&format!("Key::from_primitive failed on: {:?}", &self)),
                    input3::KeyEventType::from_primitive(self.type_)
                        .expect(&format!("KeyEventType::from_primitive failed on: {:?}", &self)),
                    Duration::from_millis(self.duration_millis),
                )
            }
        }

        let v = Vec::deserialize(deserializer)?;
        Ok(v.into_iter().map(|a: TimedKeyEventDes| a.into()).collect())
    }
}

/// Replays the sequence of events (see [Replayer::replay]) with the correct timing.
struct Replayer<'a> {
    // Invariant: pressed_keys.iter() must use ascending iteration
    // ordering.
    pressed_keys: std::collections::BTreeSet<input::Key>,
    // The input device registry to use.
    registry: &'a mut dyn InputDeviceRegistry,
}

impl<'a> Replayer<'a> {
    fn new(registry: &'a mut dyn InputDeviceRegistry) -> Self {
        Replayer { pressed_keys: std::collections::BTreeSet::new(), registry }
    }

    /// Replays the given sequence of key events with the correct timing spacing
    /// between the events.
    ///
    /// All timing in [TimedKeyEvent] is relative to the instance in the monotonic clock base at which
    /// we started replaying the entire event sequence.  The replay returns an error in case
    /// the events are not sequenced with strictly increasing timestamps.
    async fn replay<'b: 'a>(&mut self, events: &'b [TimedKeyEvent]) -> Result<(), Error> {
        let mut last_key_event_at = Duration::from_micros(0);

        // Verify that the key events are scheduled in a nondecreasing timestamp sequence.
        for key_event in events {
            if key_event.duration_since_start < last_key_event_at {
                return Err(anyhow::anyhow!(
                    concat!(
                        "TimedKeyEvent was requested out of sequence: ",
                        "TimedKeyEvent: {:?}, low watermark for duration_since_start: {:?}"
                    ),
                    &key_event,
                    last_key_event_at
                ));
            }
            if key_event.duration_since_start == last_key_event_at {
                // If you see this error message, read the documentation for how to send key events
                // correctly in the TimedKeyEvent documentation.
                return Err(anyhow::anyhow!(
                    concat!(
                        "TimedKeyEvent was requested at the same time instant as a previous event. ",
                        "This is not allowed, each key event must happen at a distinct timestamp: ",
                        "TimedKeyEvent: {:?}, low watermark for duration_since_start: {:?}"
                    ),
                    &key_event,
                    last_key_event_at
                ));
            }
            last_key_event_at = key_event.duration_since_start;
        }

        let mut input_device = self.registry.add_keyboard_device()?;
        let started_at = monotonic_nanos()?;
        for key_event in events {
            use input3::KeyEventType;
            match key_event.event_type {
                KeyEventType::Pressed | KeyEventType::Sync => {
                    self.pressed_keys.insert(key_event.key.clone());
                }
                KeyEventType::Released | KeyEventType::Cancel => {
                    self.pressed_keys.remove(&key_event.key);
                }
            }

            // The sequence below should be an async task.  The complicating factor is that
            // input_device lifetime needs to be 'static for this to be schedulable on a
            // fuchsia::async::Task. So for the time being, we skip that part.
            let processed_at = Duration::from_nanos(monotonic_nanos()? - started_at);
            let desired_at = &key_event.duration_since_start;
            if processed_at < *desired_at {
                fasync::Timer::new(fasync::Time::after((*desired_at - processed_at).into())).await;
            }
            input_device.key_press_raw(self.make_input_report(), monotonic_nanos()?)?;
        }
        input_device.serve_reports().await
    }

    /// Creates a keyboard report based on the keys that are currently pressed.
    ///
    /// The pressed keys are always reported in the nondecreasing order of their respective key
    /// codes, so a single distinct key chord will be always reported as a single distinct
    /// `KeyboardReport`.
    fn make_input_report(&self) -> KeyboardReport {
        KeyboardReport { pressed_keys: self.pressed_keys.iter().map(|k| *k as u32).collect() }
    }
}

/// Dispatches the supplied `events` into  a keyboard device registered into `registry`, honoring
/// the timing sequence that is described in them to the extent that they are possible to schedule.
pub(crate) async fn dispatch_key_events_async(
    events: &[TimedKeyEvent],
    registry: &mut dyn InputDeviceRegistry,
) -> Result<(), Error> {
    Replayer::new(registry).replay(events).await
}

pub(crate) async fn keyboard_event(
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
    )?;

    input_device.serve_reports().await
}

pub(crate) async fn text(
    input: String,
    key_event_duration: Duration,
    registry: &mut dyn InputDeviceRegistry,
) -> Result<(), Error> {
    let mut input_device = registry.add_keyboard_device()?;
    let key_sequence = InverseKeymap::new(&keymaps::US_QWERTY)
        .derive_key_sequence(&input)
        .ok_or_else(|| anyhow::format_err!("Cannot translate text to key sequence"))?;

    fx_log_debug!(
        "synthesizer::text: input: {:}; derived key sequence: {:?}, duration: {:?}",
        &input,
        &key_sequence,
        &key_event_duration,
    );
    let mut key_iter = key_sequence.into_iter().peekable();
    while let Some(keyboard) = key_iter.next() {
        input_device.key_press(keyboard, monotonic_nanos()?)?;
        if key_iter.peek().is_some() {
            thread::sleep(key_event_duration);
        }
    }

    input_device.serve_reports().await
}

pub async fn tap_event(
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
    )?;

    input_device.serve_reports().await
}

pub(crate) async fn multi_finger_tap_event(
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
    )?;

    input_device.serve_reports().await
}

pub(crate) async fn swipe(
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
    .await
}

pub(crate) async fn multi_finger_swipe(
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
    )?;

    input_device.serve_reports().await
}

#[cfg(test)]
mod tests {
    use serde::Deserialize;
    use {super::*, fuchsia_async as fasync};

    #[derive(Deserialize, Debug, Eq, PartialEq)]
    struct KeyEventsRequest {
        #[serde(default, deserialize_with = "TimedKeyEvent::vec")]
        pub key_events: Vec<TimedKeyEvent>,
    }

    #[test]
    fn deserialize_key_event() -> Result<(), Error> {
        let request_json = r#"{
          "key_events": [
            {
              "key": 458756,
              "duration_millis": 100,
              "type": 1
            }
          ]
        }"#;
        let event: KeyEventsRequest = serde_json::from_str(&request_json)?;
        assert_eq!(
            event,
            KeyEventsRequest {
                key_events: vec![TimedKeyEvent {
                    key: input::Key::A,
                    duration_since_start: Duration::from_millis(100),
                    event_type: input3::KeyEventType::Pressed,
                },],
            }
        );
        Ok(())
    }

    #[test]
    fn deserialize_key_event_maformed_input() {
        let tests: Vec<&'static str> = vec![
            // "type" has a wrong value.
            r#"{
              "key_events": [
                {
                  "key": 458756,
                  "duration_millis": 100,
                  "type": 99999,
                }
              ]
            }"#,
            // "key" has a value that is too small.
            r#"{
              "key_events": [
                {
                  "key": 12,
                  "duration_millis": 100,
                  "type": 1,
                }
              ]
            }"#,
            // "type" is missing.
            r#"{
              "key_events": [
                {
                  "key": 12,
                  "duration_millis": 100,
                }
              ]
            }"#,
            // "duration" is missing.
            r#"{
              "key_events": [
                {
                  "key": 458756,
                  "type": 1
                }
              ]
            }"#,
            // "key" is missing.
            r#"{
              "key_events": [
                {
                  "duration_millis": 100,
                  "type": 1
                }
              ]
            }"#,
        ];
        for test in tests.iter() {
            serde_json::from_str::<KeyEventsRequest>(test)
                .expect_err(&format!("malformed input should not parse: {}", &test));
        }
    }

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

        #[async_trait(?Send)]
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
                self.key_press_raw(keyboard, time)
            }

            fn key_press_raw(&mut self, keyboard: KeyboardReport, time: u64) -> Result<(), Error> {
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

            async fn serve_reports(self: Box<Self>) -> Result<(), Error> {
                Ok(())
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
            media_button_event(true, false, true, false, true, true, &mut fake_event_listener)
                .await?;
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
            keyboard_event(40, Duration::from_millis(0), &mut fake_event_listener).await?;
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
        async fn dispatch_key_events() -> Result<(), Error> {
            let mut fake_event_listener = FakeInputDeviceRegistry::new();

            // Configures a two-key chord:
            // A: _/^^^^^\___
            // B: __/^^^\____
            dispatch_key_events_async(
                &vec![
                    TimedKeyEvent::new(
                        input::Key::A,
                        input3::KeyEventType::Pressed,
                        Duration::from_millis(10),
                    ),
                    TimedKeyEvent::new(
                        input::Key::B,
                        input3::KeyEventType::Pressed,
                        Duration::from_millis(20),
                    ),
                    TimedKeyEvent::new(
                        input::Key::B,
                        input3::KeyEventType::Released,
                        Duration::from_millis(50),
                    ),
                    TimedKeyEvent::new(
                        input::Key::A,
                        input3::KeyEventType::Released,
                        Duration::from_millis(60),
                    ),
                ],
                &mut fake_event_listener,
            )
            .await?;
            assert_eq!(
                project!(fake_event_listener.get_events().await, keyboard),
                [
                    Ok(Some(KeyboardReport { pressed_keys: vec![input::Key::A as u32] })),
                    Ok(Some(KeyboardReport {
                        pressed_keys: vec![input::Key::A as u32, input::Key::B as u32]
                    })),
                    Ok(Some(KeyboardReport { pressed_keys: vec![input::Key::A as u32] })),
                    Ok(Some(KeyboardReport { pressed_keys: vec![] }))
                ]
            );
            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn dispatch_key_events_in_wrong_sequence() -> Result<(), Error> {
            let mut fake_event_listener = FakeInputDeviceRegistry::new();

            // Configures a two-key chord in the wrong temporal order.
            let result = dispatch_key_events_async(
                &vec![
                    TimedKeyEvent::new(
                        input::Key::A,
                        input3::KeyEventType::Pressed,
                        Duration::from_millis(20),
                    ),
                    TimedKeyEvent::new(
                        input::Key::B,
                        input3::KeyEventType::Pressed,
                        Duration::from_millis(10),
                    ),
                ],
                &mut fake_event_listener,
            )
            .await;
            match result {
                Err(_) => Ok(()),
                Ok(_) => Err(anyhow::anyhow!("expected error but got Ok")),
            }
        }

        #[fasync::run_singlethreaded(test)]
        async fn dispatch_key_events_with_same_timestamp() -> Result<(), Error> {
            let mut fake_event_listener = FakeInputDeviceRegistry::new();

            // Configures a two-key chord in the wrong temporal order.
            let result = dispatch_key_events_async(
                &vec![
                    TimedKeyEvent::new(
                        input::Key::A,
                        input3::KeyEventType::Pressed,
                        Duration::from_millis(20),
                    ),
                    TimedKeyEvent::new(
                        input::Key::B,
                        input3::KeyEventType::Pressed,
                        Duration::from_millis(20),
                    ),
                ],
                &mut fake_event_listener,
            )
            .await;
            match result {
                Err(_) => Ok(()),
                Ok(_) => Err(anyhow::anyhow!("expected error but got Ok")),
            }
        }

        #[fasync::run_singlethreaded(test)]
        async fn text_event_report() -> Result<(), Error> {
            let mut fake_event_listener = FakeInputDeviceRegistry::new();
            text("A".to_string(), Duration::from_millis(0), &mut fake_event_listener).await?;
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
            )
            .await?;
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
            tap_event(10, 10, 1000, 1000, 1, Duration::from_millis(0), &mut fake_event_listener)
                .await?;
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
            )
            .await?;
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
            )
            .await?;
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
            )
            .await?;
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
            )
            .await?;
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
            )
            .await?;
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
            media_button_event(true, false, true, false, true, true, &mut fake_event_listener)
                .await?;

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

        #[async_trait(?Send)]
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

            fn key_press_raw(
                &mut self,
                _keyboard: KeyboardReport,
                _time: u64,
            ) -> Result<(), Error> {
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

            async fn serve_reports(self: Box<Self>) -> Result<(), Error> {
                Ok(())
            }
        }

        #[fasync::run_until_stalled(test)]
        async fn media_button_event_registers_media_buttons_device() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            media_button_event(false, false, false, false, false, false, &mut registry).await?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::MediaButtons]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn keyboard_event_registers_keyboard() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            keyboard_event(40, Duration::from_millis(0), &mut registry).await?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::Keyboard]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn text_event_registers_keyboard() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            text("A".to_string(), Duration::from_millis(0), &mut registry).await?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::Keyboard]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn multi_finger_tap_event_registers_touchscreen() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            multi_finger_tap_event(vec![], 1000, 1000, 1, Duration::from_millis(0), &mut registry)
                .await?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::Touchscreen]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn tap_event_registers_touchscreen() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            tap_event(0, 0, 1000, 1000, 1, Duration::from_millis(0), &mut registry).await?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::Touchscreen]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn swipe_registers_touchscreen() -> Result<(), Error> {
            let mut registry = FakeInputDeviceRegistry::new();
            swipe(0, 0, 1, 1, 1000, 1000, 1, Duration::from_millis(0), &mut registry).await?;
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
            )
            .await?;
            assert_matches!(registry.device_types.as_slice(), [DeviceType::Touchscreen]);
            Ok(())
        }
    }
}
