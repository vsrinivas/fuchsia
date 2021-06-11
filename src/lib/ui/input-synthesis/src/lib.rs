// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::synthesizer::*,
    anyhow::{format_err, Error},
    fidl_fuchsia_ui_input::{self, KeyboardReport, Touch},
    fuchsia_component::client::new_protocol_connector,
    keymaps::{
        inverse_keymap::{InverseKeymap, Shift},
        usages::{self, Usages},
    },
    std::time::Duration,
};

pub mod legacy_backend;
pub mod synthesizer;

mod modern_backend;

/// Simulates a media button event.
pub async fn media_button_event_command(
    volume_up: bool,
    volume_down: bool,
    mic_mute: bool,
    reset: bool,
    pause: bool,
    camera_disable: bool,
) -> Result<(), Error> {
    media_button_event(
        volume_up,
        volume_down,
        mic_mute,
        reset,
        pause,
        camera_disable,
        get_backend().await?.as_mut(),
    )
    .await
}

/// Simulates a key press of specified `usage`.
///
/// `key_event_duration` is the time spent between key-press and key-release events.
///
/// # Resolves to
/// * `Ok(())` if the events were successfully injected.
/// * `Err(Error)` otherwise.
///
/// # Corner case handling
/// * `key_event_duration` of zero is permitted, and will result in events being generated as
///    quickly as possible.
///
/// # Future directions
/// Per fxbug.dev/63532, this method will be replaced with a method that deals in
/// `fuchsia.input.Key`s, instead of HID Usage IDs.
pub async fn keyboard_event_command(usage: u32, key_event_duration: Duration) -> Result<(), Error> {
    keyboard_event(usage, key_event_duration, get_backend().await?.as_mut()).await
}

/// Simulates `input` being typed on a keyboard, with `key_event_duration` between key events.
///
/// # Requirements
/// * `input` must be non-empty
/// * `input` must only contain characters representable using the current keyboard layout
///    and locale. (At present, it is assumed that the current layout and locale are
///   `US-QWERTY` and `en-US`, respectively.)
///
/// # Resolves to
/// * `Ok(())` if the arguments met the requirements above, and the events were successfully
///   injected.
/// * `Err(Error)` otherwise.
///
/// # Corner case handling
/// * `key_event_duration` of zero is permitted, and will result in events being generated as
///    quickly as possible.
pub async fn text_command(input: String, key_event_duration: Duration) -> Result<(), Error> {
    text(input, key_event_duration, get_backend().await?.as_mut()).await
}

/// Simulates a sequence of key events (presses and releases) on a keyboard.
///
/// Dispatches the supplied `events` into a keyboard device, honoring the timing sequence that is
/// requested in them, to the extent possible using the current scheduling system.
///
/// Since each individual key press is broken down into constituent pieces (presses, releases,
/// pauses), it is possible to dispatch a key event sequence corresponding to multiple keys being
/// pressed and released in an arbitrary sequence.  This sequence is usually understood as a timing
/// diagram like this:
///
/// ```ignore
///           v--- key press   v--- key release
/// A: _______/^^^^^^^^^^^^^^^^\__________
///    |<----->|   <-- duration from start for key press.
///    |<--------------------->|   <-- duration from start for key release.
///
/// B: ____________/^^^^^^^^^^^^^^^^\_____
///                ^--- key press   ^--- key release
///    |<--------->|   <-- duration from start for key press.
///    |<-------------------------->|   <-- duration for key release.
/// ```
///
/// You would from there convert the desired timing diagram into a sequence of [TimedKeyEvent]s
/// that you would pass into this function. Note that all durations are specified as durations
/// from the start of the key event sequence.
///
/// Note that due to the way event timing works, it is in practice impossible to have two key
/// events happen at exactly the same time even if you so specify.  Do not rely on simultaneous
/// asynchronous event processing: it does not work in this code, and it is not how reality works
/// either.  Instead, design your key event processing so that it is robust against the inherent
/// non-determinism in key event delivery.
pub async fn dispatch_key_events(events: &[TimedKeyEvent]) -> Result<(), Error> {
    dispatch_key_events_async(events, get_backend().await?.as_mut()).await
}

/// Simulates `tap_event_count` taps at coordinates `(x, y)` for a touchscreen with horizontal
/// resolution `width` and vertical resolution `height`. `(x, y)` _should_ be specified in absolute
/// coordinations, with `x` normally in the range (0, `width`), `y` normally in the range
/// (0, `height`).
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
    tap_event(x, y, width, height, tap_event_count, duration, get_backend().await?.as_mut()).await
}

/// Simulates `tap_event_count` times to repeat the multi-finger-taps, for touchscreen with
/// horizontal resolution `width` and vertical resolution `height`. Finger positions _should_
/// be specified in absolute coordinations, with `x` values normally in the range (0, `width`),
/// and `y` values normally in the range (0, `height`).
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
        get_backend().await?.as_mut(),
    )
    .await
}

/// Simulates swipe from coordinates `(x0, y0)` to `(x1, y1)` for a touchscreen with
/// horizontal resolution `width` and vertical resolution `height`, with `move_event_count`
/// touch-move events in between. Positions for move events are linearly interpolated.
///
/// Finger positions _should_ be specified in absolute coordinations, with `x` values normally in the
/// range (0, `width`), and `y` values normally in the range (0, `height`).
///
/// `duration` is the total time from the touch-down event to the touch-up event, inclusive
/// of all move events in between.
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
    swipe(x0, y0, x1, y1, width, height, move_event_count, duration, get_backend().await?.as_mut())
        .await
}

/// Simulates swipe with fingers starting at `start_fingers`, and moving to `end_fingers`,
/// for a touchscreen for a touchscreen with horizontal resolution `width` and vertical resolution
/// `height`. Finger positions _should_ be specified in absolute coordinations, with `x` values
/// normally in the range (0, `width`), and `y` values normally in the range (0, `height`).
///
/// Linearly interpolates `move_event_count` touch-move events between the start positions
/// and end positions, over `duration` time. (`duration` is the total time from the touch-down
/// event to the touch-up event, inclusive of all move events in between.)
///
/// # Requirements
/// * `start_fingers` and `end_fingers` must have the same length
/// * `start_fingers.len()` and `end_finger.len()` must be representable within a `u32`
///
/// # Resolves to
/// * `Ok(())` if the arguments met the requirements above, and the events were successfully
///   injected.
/// * `Err(Error)` otherwise.
///
/// # Corner case handling
/// * `move_event_count` of zero is permitted, and will result in just the DOWN and UP events
///   being generated.
/// * `duration.as_nanos() < move_event_count` is allowed, and will result in all events having
///   the same timestamp.
/// * `width` and `height` are permitted to be zero; such values are left to the interpretation
///   of the system under test.
/// * finger positions _may_ exceed the expected bounds; such values are left to the interpretation
///   of the sytem under test.
pub async fn multi_finger_swipe_command(
    start_fingers: Vec<(u32, u32)>,
    end_fingers: Vec<(u32, u32)>,
    width: u32,
    height: u32,
    move_event_count: usize,
    duration: Duration,
) -> Result<(), Error> {
    multi_finger_swipe(
        start_fingers,
        end_fingers,
        width,
        height,
        move_event_count,
        duration,
        get_backend().await?.as_mut(),
    )
    .await
}

/// Selects an injection protocol, and returns the corresponding implementation
/// of `synthesizer::InputDeviceRegistry`.
///
/// # Returns
/// * Ok(`modern_backend::InputDeviceRegistry`) if `use_modern_input_injection` is true and
///   `fuchsia.input.injection.InputDeviceRegistry` is available.
/// * Ok(`legacy_backend::InputDeviceRegistry`) if `use_modern_input_injection` is false and
///   `fuchsia.ui.input.InputDeviceRegistry` is available.
/// * Err otherwise. E.g.,
///   * Neither protocol was available.
///   * Access to `/svc` was denied.
async fn get_backend() -> Result<Box<dyn InputDeviceRegistry>, Error> {
    if cfg!(use_modern_input_injection) {
        let modern_registry =
            new_protocol_connector::<fidl_fuchsia_input_injection::InputDeviceRegistryMarker>()?;
        if modern_registry.exists().await? {
            return Ok(Box::new(modern_backend::InputDeviceRegistry::new(
                modern_registry.connect()?,
            )));
        }
    } else {
        let legacy_registry =
            new_protocol_connector::<fidl_fuchsia_ui_input::InputDeviceRegistryMarker>()?;
        if legacy_registry.exists().await? {
            return Ok(Box::new(legacy_backend::InputDeviceRegistry::new()));
        }
    }

    Err(format_err!("no available InputDeviceRegistry"))
}

/// Converts the `input` string into a key sequence under the `InverseKeymap` derived from `keymap`.
///
/// This is intended for end-to-end and input testing only; for production use cases and general
/// testing, IME injection should be used instead.
///
/// A translation from `input` to a sequence of keystrokes is not guaranteed to exist. If a
/// translation does not exist, `None` is returned.
///
/// The sequence does not contain pauses except between repeated keys or to clear a shift state,
/// though the sequence does terminate with an empty report (no keys pressed). A shift key
/// transition is sent in advance of each series of keys that needs it.
///
/// Note that there is currently no way to distinguish between particular key releases. As such,
/// only one key release report is generated even in combinations, e.g. Shift + A.
///
/// # Example
///
/// ```
/// let key_sequence = derive_key_sequence(&keymaps::US_QWERTY, "A").unwrap();
///
/// // [shift, A, clear]
/// assert_eq!(key_sequence.len(), 3);
/// ```
fn derive_key_sequence(keymap: &keymaps::Keymap<'_>, input: &str) -> Option<Vec<KeyboardReport>> {
    let inverse_keymap = InverseKeymap::new(keymap);
    let mut reports = vec![];
    let mut shift_pressed = false;
    let mut last_usage = None;

    for ch in input.chars() {
        let key_stroke = inverse_keymap.get(&ch)?;

        match key_stroke.shift {
            Shift::Yes if !shift_pressed => {
                shift_pressed = true;
                last_usage = Some(0);
            }
            Shift::No if shift_pressed => {
                shift_pressed = false;
                last_usage = Some(0);
            }
            _ => {
                if last_usage == Some(key_stroke.usage) {
                    last_usage = Some(0);
                }
            }
        }

        if let Some(0) = last_usage {
            reports.push(KeyboardReport {
                pressed_keys: if shift_pressed {
                    vec![Usages::HidUsageKeyLeftShift as u32]
                } else {
                    vec![]
                },
            });
        }

        last_usage = Some(key_stroke.usage);

        reports.push(KeyboardReport {
            pressed_keys: if shift_pressed {
                vec![key_stroke.usage, Usages::HidUsageKeyLeftShift as u32]
            } else {
                vec![key_stroke.usage]
            },
        });
    }

    // TODO: In the future, we might want to distinguish between different key releases, instead
    //       of sending one single release report even in the case of key combinations.
    reports.push(KeyboardReport { pressed_keys: vec![] });

    Some(reports)
}

#[cfg(test)]
mod tests {
    // Most of the functions in this file need to bind to FIDL services in
    // this component's environment to do their work, but a component can't
    // modify its own environment. Hence, we can't validate those functions.
    //
    // However, we can (and do) validate derive_key_sequence().

    use super::{derive_key_sequence, KeyboardReport, Usages};

    macro_rules! reports {
        ( $( [ $( $usages:expr ),* ] ),* $( , )? ) => {
            Some(vec![
                $(
                    KeyboardReport {
                        pressed_keys: vec![$($usages as u32),*]
                    }
                ),*
            ])
        }
    }

    #[test]
    fn lowercase() {
        assert_eq!(
            derive_key_sequence(&keymaps::US_QWERTY, "lowercase"),
            reports![
                [Usages::HidUsageKeyL],
                [Usages::HidUsageKeyO],
                [Usages::HidUsageKeyW],
                [Usages::HidUsageKeyE],
                [Usages::HidUsageKeyR],
                [Usages::HidUsageKeyC],
                [Usages::HidUsageKeyA],
                [Usages::HidUsageKeyS],
                [Usages::HidUsageKeyE],
                [],
            ]
        );
    }

    #[test]
    fn numerics() {
        assert_eq!(
            derive_key_sequence(&keymaps::US_QWERTY, "0123456789"),
            reports![
                [Usages::HidUsageKey0],
                [Usages::HidUsageKey1],
                [Usages::HidUsageKey2],
                [Usages::HidUsageKey3],
                [Usages::HidUsageKey4],
                [Usages::HidUsageKey5],
                [Usages::HidUsageKey6],
                [Usages::HidUsageKey7],
                [Usages::HidUsageKey8],
                [Usages::HidUsageKey9],
                [],
            ]
        );
    }

    #[test]
    fn internet_text_entry() {
        assert_eq!(
            derive_key_sequence(&keymaps::US_QWERTY, "http://127.0.0.1:8080"),
            reports![
                [Usages::HidUsageKeyH],
                [Usages::HidUsageKeyT],
                [],
                [Usages::HidUsageKeyT],
                [Usages::HidUsageKeyP],
                // ':'
                // Shift is actuated first on its own, then together with
                // the key.
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeySemicolon, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKeySlash],
                [],
                [Usages::HidUsageKeySlash],
                [Usages::HidUsageKey1],
                [Usages::HidUsageKey2],
                [Usages::HidUsageKey7],
                [Usages::HidUsageKeyDot],
                [Usages::HidUsageKey0],
                [Usages::HidUsageKeyDot],
                [Usages::HidUsageKey0],
                [Usages::HidUsageKeyDot],
                [Usages::HidUsageKey1],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeySemicolon, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKey8],
                [Usages::HidUsageKey0],
                [Usages::HidUsageKey8],
                [Usages::HidUsageKey0],
                [],
            ]
        );
    }

    #[test]
    fn sentence() {
        assert_eq!(
            derive_key_sequence(&keymaps::US_QWERTY, "Hello, world!"),
            reports![
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyH, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKeyE],
                [Usages::HidUsageKeyL],
                [],
                [Usages::HidUsageKeyL],
                [Usages::HidUsageKeyO],
                [Usages::HidUsageKeyComma],
                [Usages::HidUsageKeySpace],
                [Usages::HidUsageKeyW],
                [Usages::HidUsageKeyO],
                [Usages::HidUsageKeyR],
                [Usages::HidUsageKeyL],
                [Usages::HidUsageKeyD],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKey1, Usages::HidUsageKeyLeftShift],
                [],
            ]
        );
    }

    #[test]
    fn hold_shift() {
        assert_eq!(
            derive_key_sequence(&keymaps::US_QWERTY, "ALL'S WELL!"),
            reports![
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyA, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyL, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyL, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKeyApostrophe],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyS, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeySpace, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyW, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyE, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyL, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyL, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKey1, Usages::HidUsageKeyLeftShift],
                [],
            ]
        );
    }
}
