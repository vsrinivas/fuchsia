// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! Functionality for parsing events on Linux's `/dev/input/eventX` character
//! devices.

use std::fs::OpenOptions;
use std::io::{Error, Read};

/// Input event types, used in the Linux `input_event` struct.
///
/// See Linux `include/uapi/linux/input.h` for details.
#[repr(u16)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
#[allow(dead_code)]
pub enum EventType {
    Syn = 0x00,
    Key = 0x01,
    Rel = 0x02,
    Abs = 0x03,
    Msc = 0x04,
    Sw = 0x05,
    Led = 0x11,
    Snd = 0x12,
    Rep = 0x14,
    Ff = 0x15,
    Pwr = 0x16,
    FfStatus = 0x17,
}

/// Linux's `struct time_val`.
///
/// See Linux `include/uapi/linux/time.h` for details.
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, Default)]
pub struct TimeVal {
    pub tv_sec: libc::c_long,
    pub tv_usec: libc::c_long,
}

/// Linux's `struct input_event`.
///
/// This is the format returned by input devices such as `/dev/input/eventX', which provide raw
/// input events from input devices.
///
/// See Linux `include/uapi/linux/input.h` for details.
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, Default)]
pub struct InputEvent {
    pub time: TimeVal,
    pub type_: u16,
    pub code: u16,
    pub value: i32,
}

// Keyboard `value` values.
#[repr(i32)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
#[allow(dead_code)]
pub enum KeyboardValue {
    KeyDown = 0,
    KeyUp = 1,
    KeyRepeat = 2,
}

// Keyboard `code` values.
//
// This is not the complete list, but just a subset needed for tests.
//
// See Linux's `include/uapi/linux/input.h` for details.
#[repr(u16)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum KeyboardCode {
    A = 30,
    B = 48,
    C = 46,
    LeftShift = 42,
}

/// An EventReader parses events from the `/dev/input/eventX` interface.
pub struct EventReader {
    input: Box<dyn Read + Send>,
}

impl EventReader {
    /// Create a new EventReader, parsing bytes from the given reader.
    pub fn new_from_reader(reader: Box<dyn Read + Send>) -> EventReader {
        EventReader { input: reader }
    }

    /// Create a new EventReader from the given file. The input file may be a character special
    /// file, such as `/dev/input/eventX`.
    pub fn new_from_path(path: &std::path::Path) -> Result<EventReader, Error> {
        let input = OpenOptions::new().read(true).write(false).create(false).open(&path)?;
        Ok(EventReader::new_from_reader(Box::new(input)))
    }

    pub fn read(self: &mut EventReader) -> Result<InputEvent, Error> {
        let mut result: InputEvent = Default::default();

        // Read an InputEvent from the input stream by reading the raw bytes into "result".
        //
        // The Linux kernel requires us to perform the read of a full event in a single read()
        // syscall.
        unsafe {
            let raw_bytes = std::slice::from_raw_parts_mut(
                &mut result as *mut _ as *mut u8,
                std::mem::size_of::<InputEvent>(),
            );
            self.input.read_exact(raw_bytes)?;
        }

        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

    #[test]
    fn test_parse_event() {
        // Raw bytes of event pulled from /dev/input/event0 from USB Keyboard pressing the "a"
        // button.
        #[rustfmt::skip]
        let raw_event = vec![
            /* 0x00 */ 0x5b, 0x98, 0xe4, 0x5c, 0x00, 0x00, 0x00, 0x00,
            /* 0x08 */ 0x33, 0xfd, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* 0x10 */ 0x01, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00,
        ];

        let mut reader = EventReader::new_from_reader(Box::new(std::io::Cursor::new(raw_event)));
        let event = reader.read().expect("Could not parse event");
        assert_eq!({ event.type_ }, EventType::Key as u16);
        assert_eq!({ event.code }, KeyboardCode::A as u16);
        assert_eq!({ event.value }, KeyboardValue::KeyDown as i32);
    }

    #[test]
    fn test_eof() {
        let empty_stream = vec![];
        let mut reader = EventReader::new_from_reader(Box::new(std::io::Cursor::new(empty_stream)));
        assert_matches!(reader.read(), Err(_));
    }
}
