// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::wire, fidl_fuchsia_input::Key, fidl_fuchsia_ui_input3 as input3};

const HID_USAGE_PAGE_KEYBOARD: u16 = 0x0007;
const HID_USAGE_PAGE_CONSUMER: u16 = 0x000c;

const NO_KEY_MAPPING: u16 = 0;

//
// This array maps HID usage values to the evdev keycodes that should be sent to the guest.
//
// Some more details about mappings can be found here:
//
// https://chromium.googlesource.com/chromium/src/+/dff16958029d9a8fb9004351f72e961ed4143e83/ui/events/keycodes/dom/keycode_converter_data.inc
//
const KEYBOARD_PAGE_KEYMAP: [u16; 232] = [
    NO_KEY_MAPPING, // Reserved
    NO_KEY_MAPPING, // Keyboard ErrorRollOver
    NO_KEY_MAPPING, // Keyboard POSTFail
    NO_KEY_MAPPING, // Keyboard ErrorUndefined
    wire::KEY_A,
    wire::KEY_B,
    wire::KEY_C,
    wire::KEY_D,
    wire::KEY_E,
    wire::KEY_F,
    wire::KEY_G,
    wire::KEY_H,
    wire::KEY_I,
    wire::KEY_J,
    wire::KEY_K,
    wire::KEY_L,
    wire::KEY_M,
    wire::KEY_N,
    wire::KEY_O,
    wire::KEY_P,
    wire::KEY_Q,
    wire::KEY_R,
    wire::KEY_S,
    wire::KEY_T,
    wire::KEY_U,
    wire::KEY_V,
    wire::KEY_W,
    wire::KEY_X,
    wire::KEY_Y,
    wire::KEY_Z,
    wire::KEY_1,
    wire::KEY_2,
    wire::KEY_3,
    wire::KEY_4,
    wire::KEY_5,
    wire::KEY_6,
    wire::KEY_7,
    wire::KEY_8,
    wire::KEY_9,
    wire::KEY_0,
    wire::KEY_ENTER,
    wire::KEY_ESC,
    wire::KEY_BACKSPACE,
    wire::KEY_TAB,
    wire::KEY_SPACE,
    wire::KEY_MINUS,
    wire::KEY_EQUAL,
    wire::KEY_LEFTBRACE,
    wire::KEY_RIGHTBRACE,
    wire::KEY_BACKSLASH,
    // This key is 'NonUsHash'. This should never appear with the BACKSLASH key so we just use the
    // same keycode for both.
    //
    // See: https://chromium.googlesource.com/chromium/src/+/dff16958029d9a8fb9004351f72e961ed4143e83/ui/events/keycodes/dom/keycode_converter_data.inc#179
    wire::KEY_BACKSLASH,
    wire::KEY_SEMICOLON,
    wire::KEY_APOSTROPHE,
    wire::KEY_GRAVE,
    wire::KEY_COMMA,
    wire::KEY_DOT,
    wire::KEY_SLASH,
    wire::KEY_CAPSLOCK,
    wire::KEY_F1,
    wire::KEY_F2,
    wire::KEY_F3,
    wire::KEY_F4,
    wire::KEY_F5,
    wire::KEY_F6,
    wire::KEY_F7,
    wire::KEY_F8,
    wire::KEY_F9,
    wire::KEY_F10,
    wire::KEY_F11,
    wire::KEY_F12,
    wire::KEY_SYSRQ,
    wire::KEY_SCROLLLOCK,
    wire::KEY_PAUSE,
    wire::KEY_INSERT,
    wire::KEY_HOME,
    wire::KEY_PAGEUP,
    wire::KEY_DELETE,
    wire::KEY_END,
    wire::KEY_PAGEDOWN,
    wire::KEY_RIGHT,
    wire::KEY_LEFT,
    wire::KEY_DOWN,
    wire::KEY_UP,
    wire::KEY_NUMLOCK,
    wire::KEY_KPSLASH,
    wire::KEY_KPASTERISK,
    wire::KEY_KPMINUS,
    wire::KEY_KPPLUS,
    wire::KEY_KPENTER,
    wire::KEY_KP1,
    wire::KEY_KP2,
    wire::KEY_KP3,
    wire::KEY_KP4,
    wire::KEY_KP5,
    wire::KEY_KP6,
    wire::KEY_KP7,
    wire::KEY_KP8,
    wire::KEY_KP9,
    wire::KEY_KP0,
    wire::KEY_KPDOT,
    wire::KEY_102ND,
    wire::KEY_COMPOSE,
    wire::KEY_POWER,
    wire::KEY_KPEQUAL,
    wire::KEY_F13,
    wire::KEY_F14,
    wire::KEY_F15,
    wire::KEY_F16,
    wire::KEY_F17,
    wire::KEY_F18,
    wire::KEY_F19,
    wire::KEY_F20,
    wire::KEY_F21,
    wire::KEY_F22,
    wire::KEY_F23,
    wire::KEY_F24,
    wire::KEY_OPEN,
    wire::KEY_HELP,
    wire::KEY_PROPS,
    wire::KEY_FRONT,
    wire::KEY_STOP,
    wire::KEY_AGAIN,
    wire::KEY_UNDO,
    wire::KEY_CUT,
    wire::KEY_COPY,
    wire::KEY_PASTE,
    wire::KEY_FIND,
    wire::KEY_MUTE,
    wire::KEY_VOLUMEUP,
    wire::KEY_VOLUMEDOWN,
    // Skip some more esoteric keys that have no obvious evdev counterparts.
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    NO_KEY_MAPPING,
    wire::KEY_LEFTCTRL,
    wire::KEY_LEFTSHIFT,
    wire::KEY_LEFTALT,
    wire::KEY_LEFTMETA,
    wire::KEY_RIGHTCTRL,
    wire::KEY_RIGHTSHIFT,
    wire::KEY_RIGHTALT,
    wire::KEY_RIGHTMETA,
];

fn decode_key(key: Key) -> Option<u16> {
    // Fuchsia keycode are modeled after HID usage values, with the upper 16 bits representing the
    // usage page and the bottom 16 bits representing the usage value.
    let val = key.into_primitive();
    let page = (val >> 16) as u16;
    let usage = (val & 0xffff) as u16;
    match page {
        HID_USAGE_PAGE_KEYBOARD => translate_keyboard_page_event(usage),
        HID_USAGE_PAGE_CONSUMER => translate_consumer_page_event(usage),
        _ => None,
    }
}

fn translate_keyboard_page_event(usage: u16) -> Option<u16> {
    let index = usize::try_from(usage).unwrap();
    if index >= KEYBOARD_PAGE_KEYMAP.len() {
        return None;
    }
    let value = KEYBOARD_PAGE_KEYMAP[index];
    if value == NO_KEY_MAPPING {
        None
    } else {
        Some(value)
    }
}

fn translate_consumer_page_event(usage: u16) -> Option<u16> {
    // HID values are from HID Usage Tables: Section 15: Consumer Page
    match usage {
        0xe2 => Some(wire::KEY_MUTE),
        0xe9 => Some(wire::KEY_VOLUMEUP),
        0xea => Some(wire::KEY_VOLUMEDOWN),
        _ => None,
    }
}

fn decode_value(event_type: input3::KeyEventType) -> Option<u32> {
    match event_type {
        input3::KeyEventType::Pressed => Some(wire::VIRTIO_INPUT_EV_KEY_PRESSED),
        input3::KeyEventType::Released => Some(wire::VIRTIO_INPUT_EV_KEY_RELEASED),
        // The FIDL documentation specifically specifies these should not be handled as PRESS and
        // RELEASE events, so we ignore them.
        input3::KeyEventType::Sync => None,
        input3::KeyEventType::Cancel => None,
    }
}

fn decode_type(event: &input3::KeyEvent) -> u16 {
    // There is a different event type for key repeats vs key press/release events. In the Fuchsia
    // input event a key repeat is specified by the repeat sequence entry in the event.
    if event.repeat_sequence.is_some() {
        wire::VIRTIO_INPUT_EV_REP
    } else {
        wire::VIRTIO_INPUT_EV_KEY
    }
}

pub fn translate_keyboard_event(event: input3::KeyEvent) -> Option<[wire::VirtioInputEvent; 2]> {
    let value = decode_value(event.type_?)?;
    let type_ = decode_type(&event);
    let code = decode_key(event.key?)?;
    Some([
        wire::VirtioInputEvent { type_: type_.into(), value: value.into(), code: code.into() },
        wire::SYNC_EVENT.clone(),
    ])
}

#[cfg(test)]
mod tests {
    use super::*;

    const FUCHSIA_KEYS: [Key; 110] = [
        Key::A,
        Key::B,
        Key::C,
        Key::D,
        Key::E,
        Key::F,
        Key::G,
        Key::H,
        Key::I,
        Key::J,
        Key::K,
        Key::L,
        Key::M,
        Key::N,
        Key::O,
        Key::P,
        Key::Q,
        Key::R,
        Key::S,
        Key::T,
        Key::U,
        Key::V,
        Key::W,
        Key::X,
        Key::Y,
        Key::Z,
        Key::Key1,
        Key::Key2,
        Key::Key3,
        Key::Key4,
        Key::Key5,
        Key::Key6,
        Key::Key7,
        Key::Key8,
        Key::Key9,
        Key::Key0,
        Key::Enter,
        Key::Escape,
        Key::Backspace,
        Key::Tab,
        Key::Space,
        Key::Minus,
        Key::Equals,
        Key::LeftBrace,
        Key::RightBrace,
        Key::Backslash,
        Key::NonUsHash,
        Key::Semicolon,
        Key::Apostrophe,
        Key::GraveAccent,
        Key::Comma,
        Key::Dot,
        Key::Slash,
        Key::CapsLock,
        Key::F1,
        Key::F2,
        Key::F3,
        Key::F4,
        Key::F5,
        Key::F6,
        Key::F7,
        Key::F8,
        Key::F9,
        Key::F10,
        Key::F11,
        Key::F12,
        Key::PrintScreen,
        Key::ScrollLock,
        Key::Pause,
        Key::Insert,
        Key::Home,
        Key::PageUp,
        Key::Delete,
        Key::End,
        Key::PageDown,
        Key::Right,
        Key::Left,
        Key::Down,
        Key::Up,
        Key::NumLock,
        Key::KeypadSlash,
        Key::KeypadAsterisk,
        Key::KeypadMinus,
        Key::KeypadPlus,
        Key::KeypadEnter,
        Key::Keypad1,
        Key::Keypad2,
        Key::Keypad3,
        Key::Keypad4,
        Key::Keypad5,
        Key::Keypad6,
        Key::Keypad7,
        Key::Keypad8,
        Key::Keypad9,
        Key::Keypad0,
        Key::KeypadDot,
        Key::NonUsBackslash,
        Key::KeypadEquals,
        Key::Menu,
        Key::LeftCtrl,
        Key::LeftShift,
        Key::LeftAlt,
        Key::LeftMeta,
        Key::RightCtrl,
        Key::RightShift,
        Key::RightAlt,
        Key::RightMeta,
        Key::MediaMute,
        Key::MediaVolumeIncrement,
        Key::MediaVolumeDecrement,
    ];

    #[fuchsia::test]
    fn test_key_press() {
        // Press the 'A' key.
        const TEST_KEY: Key = Key::A;
        let events = translate_keyboard_event(input3::KeyEvent {
            type_: Some(input3::KeyEventType::Pressed),
            key: Some(TEST_KEY),
            ..input3::KeyEvent::EMPTY
        })
        .unwrap();

        // Expect 2 virtio events.
        assert_eq!(2, events.len());

        // ... a key press event
        assert_eq!(wire::VIRTIO_INPUT_EV_KEY, events[0].type_.get());
        assert_eq!(wire::VIRTIO_INPUT_EV_KEY_PRESSED, events[0].value.get());
        assert_eq!(30, events[0].code.get());

        // followed by a sync
        assert_eq!(*wire::SYNC_EVENT, events[1]);
    }

    #[fuchsia::test]
    fn test_key_repeat() {
        // Press the 'A' key with a repeat sequence.
        const TEST_KEY: Key = Key::A;
        let events = translate_keyboard_event(input3::KeyEvent {
            type_: Some(input3::KeyEventType::Pressed),
            key: Some(TEST_KEY),
            repeat_sequence: Some(1),
            ..input3::KeyEvent::EMPTY
        })
        .unwrap();

        // Expect 2 virtio events.
        assert_eq!(2, events.len());

        // ... a key repeat event
        assert_eq!(wire::VIRTIO_INPUT_EV_REP, events[0].type_.get());
        assert_eq!(wire::VIRTIO_INPUT_EV_KEY_PRESSED, events[0].value.get());
        assert_eq!(30, events[0].code.get());

        // followed by a sync
        assert_eq!(*wire::SYNC_EVENT, events[1]);
    }

    #[fuchsia::test]
    fn test_key_release() {
        // Press the 'A' key.
        const TEST_KEY: Key = Key::A;
        let events = translate_keyboard_event(input3::KeyEvent {
            type_: Some(input3::KeyEventType::Released),
            key: Some(TEST_KEY),
            ..input3::KeyEvent::EMPTY
        })
        .unwrap();

        // Expect 2 virtio events.
        assert_eq!(2, events.len());

        // ... a key release event
        assert_eq!(wire::VIRTIO_INPUT_EV_KEY, events[0].type_.get());
        assert_eq!(wire::VIRTIO_INPUT_EV_KEY_RELEASED, events[0].value.get());
        assert_eq!(30, events[0].code.get());

        // followed by a sync
        assert_eq!(*wire::SYNC_EVENT, events[1]);
    }

    #[fuchsia::test]
    fn test_all_fuchsia_keys() {
        // Test that we have a mapping for all the known fuchsia Key values.
        for key in FUCHSIA_KEYS {
            translate_keyboard_event(input3::KeyEvent {
                type_: Some(input3::KeyEventType::Pressed),
                key: Some(key),
                ..input3::KeyEvent::EMPTY
            })
            .expect(&format!("Unable to translate fuchsia key {:?}", key));
        }
    }
}
