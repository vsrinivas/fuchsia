// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A struct which can be used to convert a code point to a string.
pub struct CodePoint {
    pub code_point: u32,
    pub control_pressed: bool,
}

impl From<CodePoint> for Option<String> {
    fn from(item: CodePoint) -> Self {
        // We currently don't support higher code points.
        if item.code_point > 128 {
            return None;
        }

        let mut code_point = item.code_point;
        // Convert to a control code if we are holding ctrl.
        if item.control_pressed {
            if let Some(c) = std::char::from_u32(item.code_point) {
                match c {
                    'a'..='z' => code_point = code_point - 96,
                    'A'..='Z' => code_point = code_point - 64,
                    _ => (),
                };
            }
        }

        String::from_utf8(vec![code_point as u8]).ok()
    }
}

const HID_USAGE_KEY_ENTER: u32 = 0x28;
const HID_USAGE_KEY_ESC: u32 = 0x29;
const HID_USAGE_KEY_BACKSPACE: u32 = 0x2a;
const HID_USAGE_KEY_TAB: u32 = 0x2b;
const HID_USAGE_KEY_INSERT: u32 = 0x49;
const HID_USAGE_KEY_HOME: u32 = 0x4a;
const HID_USAGE_KEY_PAGEUP: u32 = 0x4b;
const HID_USAGE_KEY_DELETE: u32 = 0x4c;
const HID_USAGE_KEY_END: u32 = 0x4d;
const HID_USAGE_KEY_PAGEDOWN: u32 = 0x4e;
const HID_USAGE_KEY_RIGHT: u32 = 0x4f;
const HID_USAGE_KEY_LEFT: u32 = 0x50;
const HID_USAGE_KEY_DOWN: u32 = 0x51;
const HID_USAGE_KEY_UP: u32 = 0x52;

/// A struct which can be used to convert a hid_usage to a suitable string.
pub struct HidUsage(pub u32);

impl From<HidUsage> for Option<String> {
    fn from(item: HidUsage) -> Self {
        let esc: char = 0x1b_u8.into();
        macro_rules! escape_string {
            ($x:expr) => {{
                Some(format!("{}{}", esc, $x))
            }};
        }

        match item.0 {
            HID_USAGE_KEY_BACKSPACE => Some(String::from("\x7f")),
            HID_USAGE_KEY_ESC => escape_string!(""),
            HID_USAGE_KEY_PAGEDOWN => escape_string!("[6~"),
            HID_USAGE_KEY_PAGEUP => escape_string!("[5~"),
            HID_USAGE_KEY_END => escape_string!("[F"),
            HID_USAGE_KEY_HOME => escape_string!("[H"),
            HID_USAGE_KEY_LEFT => escape_string!("[D"),
            HID_USAGE_KEY_UP => escape_string!("[A"),
            HID_USAGE_KEY_RIGHT => escape_string!("[C"),
            HID_USAGE_KEY_DOWN => escape_string!("[B"),
            HID_USAGE_KEY_INSERT => escape_string!("[2~"),
            HID_USAGE_KEY_DELETE => escape_string!("[3~"),
            HID_USAGE_KEY_ENTER => Some(String::from("\n")),
            HID_USAGE_KEY_TAB => Some(String::from("\t")),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn convert_from_code_point_unsupported_values() {
        assert!(
            Option::<String>::from(CodePoint { code_point: 129, control_pressed: false }).is_none()
        );
    }

    #[test]
    fn convert_from_code_point_shifts_when_control_pressed_lowercase() {
        let mut i = 0;
        for c in (b'a'..=b'z').map(char::from) {
            i = i + 1;
            let result =
                Option::<String>::from(CodePoint { code_point: c as u32, control_pressed: true })
                    .unwrap();
            let expected = String::from_utf8(vec![i]).unwrap();
            assert_eq!(result, expected);
        }
    }

    #[test]
    fn convert_from_code_point_shifts_when_control_pressed_uppercase() {
        let mut i = 0;
        for c in (b'A'..=b'Z').map(char::from) {
            i = i + 1;
            let result =
                Option::<String>::from(CodePoint { code_point: c as u32, control_pressed: true })
                    .unwrap();

            let expected = String::from_utf8(vec![i]).unwrap();
            assert_eq!(result, expected);
        }
    }
}
