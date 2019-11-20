// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Fail,
    std::io::{self, Write},
};

/// An Event from an http sse stream
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Event {
    event_type: String,
    data: String,
}

impl Event {
    pub fn from_type_and_data(
        event_type: impl Into<String>,
        data: impl Into<String>,
    ) -> Result<Self, EventError> {
        let event_type = event_type.into();
        if event_type.contains('\r') {
            return Err(EventError::TypeHasCarriageReturn);
        }
        if event_type.contains('\n') {
            return Err(EventError::TypeHasNewline);
        }
        let data = data.into();
        if data.is_empty() {
            return Err(EventError::DataIsEmpty);
        }
        if data.contains('\r') {
            return Err(EventError::DataHasCarriageReturn);
        }
        Ok(Self { event_type, data })
    }

    /// Serialize the `Event` to bytes suitable for writing to an http sse stream.
    pub fn to_writer(&self, mut writer: impl Write) -> io::Result<()> {
        if !self.event_type.is_empty() {
            write!(&mut writer, "event: {}\n", self.event_type)?;
        }
        for line in self.data.split('\n') {
            write!(&mut writer, "data: {}\n", line)?;
        }
        writer.write_all(b"\n")?;
        Ok(())
    }

    pub fn event_type(&self) -> &str {
        &self.event_type
    }

    pub fn data(&self) -> &str {
        &self.data
    }
}

#[derive(Debug, Fail, PartialEq, Eq)]
pub enum EventError {
    #[fail(display = "event type cannot contain carriage returns")]
    TypeHasCarriageReturn,

    #[fail(display = "event type cannot contain carriage returns")]
    TypeHasNewline,

    #[fail(display = "event data cannot be empty")]
    DataIsEmpty,

    #[fail(display = "event data cannot contain carriage returns")]
    DataHasCarriageReturn,
}

#[cfg(test)]
mod tests {
    use super::*;

    fn assert_to_writer(event: &Event, expected: &str) {
        let mut bytes = vec![];
        event.to_writer(&mut bytes).unwrap();
        assert_eq!(std::str::from_utf8(&bytes).unwrap(), expected, "event: {:?}", event);
    }

    #[test]
    fn error_if_type_has_carriage_return() {
        assert_eq!(Event::from_type_and_data("\r", "data"), Err(EventError::TypeHasCarriageReturn));
    }

    #[test]
    fn error_if_type_has_newline() {
        assert_eq!(Event::from_type_and_data("\n", "data"), Err(EventError::TypeHasNewline));
    }

    #[test]
    fn error_if_data_is_empty() {
        assert_eq!(Event::from_type_and_data("", ""), Err(EventError::DataIsEmpty));
    }

    #[test]
    fn error_if_data_has_carriage_return() {
        assert_eq!(Event::from_type_and_data("", "\r"), Err(EventError::DataHasCarriageReturn));
    }

    #[test]
    fn to_writer_type_and_data() {
        let event = Event::from_type_and_data("type", "data").unwrap();
        assert_to_writer(&event, "event: type\ndata: data\n\n");
    }

    #[test]
    fn to_writer_no_type() {
        let event = Event::from_type_and_data("", "data").unwrap();
        assert_to_writer(&event, "data: data\n\n");
    }

    #[test]
    fn to_writer_data_trailing_newline() {
        let event = Event::from_type_and_data("", "data\n").unwrap();
        assert_to_writer(&event, "data: data\ndata: \n\n");
    }

    #[test]
    fn to_writer_two_line_data() {
        let event = Event::from_type_and_data("", "data1\ndata2").unwrap();
        assert_to_writer(&event, "data: data1\ndata: data2\n\n");
    }

    #[test]
    fn to_writer_two_line_data_trailing_newline() {
        let event = Event::from_type_and_data("", "data1\ndata2\n").unwrap();
        assert_to_writer(&event, "data: data1\ndata: data2\ndata: \n\n");
    }

    #[test]
    fn to_writer_consecutive_newlines() {
        let event = Event::from_type_and_data("", "\n\n").unwrap();
        assert_to_writer(&event, "data: \ndata: \ndata: \n\n");
    }
}
