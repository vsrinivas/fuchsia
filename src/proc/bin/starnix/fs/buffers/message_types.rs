// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A `Message` represents a typed segment of bytes within a `MessageBuffer`.
#[derive(Clone, PartialEq, Debug)]
pub enum Message {
    /// A packet containing arbitrary bytes.
    Packet(Packet),

    /// A control message.
    Control(Control),
}

impl From<Control> for Message {
    fn from(control: Control) -> Self {
        Message::Control(control)
    }
}

impl From<Packet> for Message {
    fn from(packet: Packet) -> Self {
        Message::Packet(packet)
    }
}

impl Message {
    /// Returns a `Message::Control` with the given bytes.
    #[cfg(test)]
    pub fn control(bytes: Vec<u8>) -> Message {
        Message::Control(Control { bytes })
    }

    /// Returns a `Message::Packet` with the given bytes.
    pub fn packet(bytes: Vec<u8>) -> Message {
        Message::Packet(Packet { bytes })
    }

    /// Returns the length of the message in bytes.
    pub fn len(&self) -> usize {
        match self {
            Message::Packet(p) => p.len(),
            Message::Control(c) => c.len(),
        }
    }
}

#[derive(Clone, PartialEq, Debug)]
pub struct Control {
    /// The bytes associated with this control message.
    bytes: Vec<u8>,
}

impl Control {
    /// Returns the number of bytes in the control message.
    pub fn len(&self) -> usize {
        self.bytes.len()
    }
}

/// A `Packet` stores an arbitrary sequence of bytes.
#[derive(Clone, PartialEq, Debug, Default)]
pub struct Packet {
    /// The bytes in this packet.
    bytes: Vec<u8>,
}

impl Packet {
    /// Returns true if the packet is empty.
    pub fn is_empty(&self) -> bool {
        self.bytes.is_empty()
    }

    /// Returns the length of the packet.
    pub fn len(&self) -> usize {
        self.bytes.len()
    }

    /// Splits the packet at `index`.
    ///
    /// After this call returns, at most `at` bytes will be stored in this `Packet`, and any
    /// remaining bytes will be moved to the returned `Packet`.
    pub fn split_off(&mut self, index: usize) -> Self {
        let mut packet = Packet::default();
        if index < self.len() {
            packet.bytes = self.bytes.split_off(index);
        }
        packet
    }

    /// Returns a reference to the bytes in the packet.
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }
}
