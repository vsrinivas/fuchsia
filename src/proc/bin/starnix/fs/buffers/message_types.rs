// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::socket::SocketAddress;

/// A `Message` represents a typed segment of bytes within a `MessageQueue`.
#[derive(Clone, PartialEq, Debug)]
pub struct Message {
    /// The data contained in the message.
    pub data: MessageData,

    /// The address from which the message was sent.
    pub address: Option<SocketAddress>,

    /// The ancillary data that is associated with this message.
    pub ancillary_data: Option<AncillaryData>,
}

impl Message {
    /// Creates a a new message with the provided message and ancillary data.
    pub fn new(
        data: MessageData,
        address: Option<SocketAddress>,
        ancillary_data: Option<AncillaryData>,
    ) -> Self {
        Message { data, address, ancillary_data }
    }

    /// Returns the length of the message in bytes.
    ///
    /// Note that ancillary data does not contribute to the length of the message.
    pub fn len(&self) -> usize {
        self.data.len()
    }
}

impl From<MessageData> for Message {
    fn from(data: MessageData) -> Self {
        Message { data, address: None, ancillary_data: None }
    }
}

impl From<Vec<u8>> for Message {
    fn from(data: Vec<u8>) -> Self {
        Self { data: data.into(), address: None, ancillary_data: None }
    }
}

#[derive(Clone, PartialEq, Debug)]
pub struct AncillaryData {
    /// The bytes associated with this control message.
    bytes: Vec<u8>,
}

impl From<Vec<u8>> for AncillaryData {
    fn from(bytes: Vec<u8>) -> Self {
        Self { bytes }
    }
}

impl AncillaryData {
    /// Returns the number of bytes in the ancillary data.
    pub fn len(&self) -> usize {
        self.bytes.len()
    }

    /// Returns a reference to the control message bytes.
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }
}

/// A `Packet` stores an arbitrary sequence of bytes.
#[derive(Clone, PartialEq, Debug, Default)]
pub struct MessageData {
    /// The bytes in this packet.
    bytes: Vec<u8>,
}

impl MessageData {
    /// Returns true if data is empty.
    pub fn is_empty(&self) -> bool {
        self.bytes.is_empty()
    }

    /// Returns the number of bytes in the message.
    pub fn len(&self) -> usize {
        self.bytes.len()
    }

    /// Splits the message data at `index`.
    ///
    /// After this call returns, at most `at` bytes will be stored in this `MessageData`, and any
    /// remaining bytes will be moved to the returned `MessageData`.
    pub fn split_off(&mut self, index: usize) -> Self {
        let mut message_data = MessageData::default();
        if index < self.len() {
            message_data.bytes = self.bytes.split_off(index);
        }
        message_data
    }

    /// Returns a reference to the bytes in the packet.
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }
}

impl From<Vec<u8>> for MessageData {
    fn from(bytes: Vec<u8>) -> Self {
        Self { bytes }
    }
}
