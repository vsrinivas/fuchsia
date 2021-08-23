// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {carnelian::input, pty::key_util::CodePoint, pty::key_util::HidUsage};

/// Converts the given keyboard event into a String suitable to send to the shell.
/// If the conversion fails for any reason None is returned instead of an Error
/// to promote performance since we do not need to handle all errors for keyboard
/// events which are not supported.
pub fn get_input_sequence_for_key_event(event: &input::keyboard::Event) -> Option<String> {
    match event.phase {
        input::keyboard::Phase::Pressed | input::keyboard::Phase::Repeat => {
            match event.code_point {
                None => HidUsage(event.hid_usage).into(),
                Some(code_point) => CodePoint {
                    code_point: code_point,
                    control_pressed: event.modifiers.is_control_only(),
                }
                .into(),
            }
        }
        _ => None,
    }
}

/// A trait which can be used to determine if a value indicates that it
/// represents a control key modifier.
trait ControlModifier {
    /// Returns true if self should be represented as a control only modifier.
    fn is_control_only(&self) -> bool;
}

impl ControlModifier for carnelian::input::Modifiers {
    fn is_control_only(&self) -> bool {
        self.control && !self.shift && !self.alt && !self.caps_lock
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn modifiers_with_control() -> carnelian::input::Modifiers {
        input::Modifiers { control: true, ..carnelian::input::Modifiers::default() }
    }

    #[test]
    fn convert_from_code_point_when_control_is_pressed() {
        let mut i = 0;
        for c in (b'a'..=b'z').map(char::from) {
            i = i + 1;
            let event = input::keyboard::Event {
                phase: input::keyboard::Phase::Pressed,
                code_point: Some(c as u32),
                hid_usage: 0,
                modifiers: modifiers_with_control(),
            };
            let result = get_input_sequence_for_key_event(&event).unwrap();
            let expected = String::from_utf8(vec![i]).unwrap();
            assert_eq!(result, expected);
        }
    }

    #[test]
    fn convert_from_hid_usage_tab() {
        const HID_USAGE_KEY_TAB: u32 = 0x2b;

        let event = input::keyboard::Event {
            phase: input::keyboard::Phase::Pressed,
            code_point: None,
            hid_usage: HID_USAGE_KEY_TAB,
            modifiers: carnelian::input::Modifiers::default(),
        };
        let result = get_input_sequence_for_key_event(&event).unwrap();
        let expected = "\t";
        assert_eq!(result, expected);
    }
}
