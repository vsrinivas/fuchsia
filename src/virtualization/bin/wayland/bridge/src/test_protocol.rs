// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::object::{ObjectRef, RequestReceiver},
    anyhow::Error,
    fuchsia_wayland_core as wl,
};

///! An example of a simple wayland interface that uses the primitives in this
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

impl wl::FromArgs for TestMessage {
    fn from_args(op: u16, _: Vec<wl::Arg>) -> Result<Self, Error> {
        match op {
            0 => Ok(TestMessage::Message1),
            1 => Ok(TestMessage::Message2),
            op => Err(wl::DecodeError::InvalidOpcode(op).into()),
        }
    }
}

impl wl::IntoMessage for TestMessage {
    type Error = wl::EncodeError;

    fn into_message(self, id: u32) -> Result<wl::Message, Self::Error> {
        let mut message = wl::Message::new();
        message.write_header(&wl::MessageHeader {
            sender: id,
            opcode: self.opcode(),
            length: 8,
        })?;
        message.rewind();
        Ok(message)
    }
}

impl wl::MessageType for TestMessage {
    fn log(&self, this: wl::ObjectId) -> String {
        format!("TestMessage@{}", this)
    }

    fn message_name(&self) -> &'static std::ffi::CStr {
        match self {
            TestMessage::Message1 => fuchsia_trace::cstr!("test_message::message1"),
            TestMessage::Message2 => fuchsia_trace::cstr!("test_message::message2"),
        }
    }
}

pub struct TestInterface;

impl wl::Interface for TestInterface {
    const NAME: &'static str = "test_interface";
    const VERSION: u32 = 0;
    // |TestMessage| contains 2 operations; neither has arguments.
    const REQUESTS: wl::MessageGroupSpec =
        wl::MessageGroupSpec(&[wl::MessageSpec(&[]), wl::MessageSpec(&[])]);
    const EVENTS: wl::MessageGroupSpec =
        wl::MessageGroupSpec(&[wl::MessageSpec(&[]), wl::MessageSpec(&[])]);
    type Request = TestMessage;
    type Event = TestMessage;
}

pub struct TestInterface2;

impl wl::Interface for TestInterface2 {
    const NAME: &'static str = "test_interface2";
    const VERSION: u32 = 0;
    // |TestMessage| contains 2 operations; neither has arguments.
    const REQUESTS: wl::MessageGroupSpec =
        wl::MessageGroupSpec(&[wl::MessageSpec(&[]), wl::MessageSpec(&[])]);
    const EVENTS: wl::MessageGroupSpec =
        wl::MessageGroupSpec(&[wl::MessageSpec(&[]), wl::MessageSpec(&[])]);
    type Request = TestMessage;
    type Event = TestMessage;
}

pub struct TestReceiver {
    request_count: usize,
}

impl TestReceiver {
    pub fn new() -> Self {
        TestReceiver { request_count: 0 }
    }

    pub fn increment_count(&mut self) {
        self.request_count += 1;
    }

    pub fn count(&self) -> usize {
        self.request_count
    }
}

impl RequestReceiver<TestInterface> for TestReceiver {
    fn receive(
        this: ObjectRef<Self>,
        request: TestMessage,
        client: &mut Client,
    ) -> Result<(), Error> {
        if let TestMessage::Message1 = request {
            this.get_mut(client)?.increment_count();
        }
        Ok(())
    }
}
