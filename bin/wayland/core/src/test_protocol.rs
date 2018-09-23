// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::*;

///! An example of a simple wayland interface that uses the primives in this
///! crate.

/// For simplicity, our test interface uses the same enum for both requests and
/// events. This means we'll just implement |Request| and |Event| for the same
/// enum instead of using two different types.
pub enum TestMessage {
    Message1,
    Message2,
}

impl TestMessage {
    pub fn opcode(&self) -> u16 {
        match self {
        TestMessage::Message1 => 0,
        TestMessage::Message2 => 1,
        }
    }
}

impl FromMessage for TestMessage {
    type Error = DecodeError;

    fn from_message(mut message: Message) -> Result<Self, Self::Error> {
        let header = message.read_header()?;
        match header.opcode {
        0 => Ok(TestMessage::Message1),
        1 => Ok(TestMessage::Message2),
        op => Err(DecodeError::InvalidOpcode(op)),
        }
    }
}

impl IntoMessage for TestMessage {
    type Error = EncodeError;

    fn into_message(self, id: u32) -> Result<Message, Self::Error> {
        let mut message = Message::new();
        message.write_header(&MessageHeader {
            sender: id,
            opcode: self.opcode(),
            length: 8,
        })?;
        message.rewind();
        Ok(message)
    }
}

pub struct TestInterface;

impl Interface for TestInterface {
    const NAME: &'static str = "test_interface";
    const VERSION: u32 = 0;
    type Request = TestMessage;
    type Event = TestMessage;
}
