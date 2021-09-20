// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::message_types::*;

use std::collections::VecDeque;

#[derive(Default)]
/// A `MessageBuffer` stores a FIFO sequence of messages.
pub struct MessageBuffer {
    /// The messages stored in the message buffer.
    ///
    /// Writes are added at the end of the queue. Reads consume from the front of the queue.
    messages: VecDeque<Message>,

    /// The total number of bytes currently in the message buffer.
    length: usize,
}

impl MessageBuffer {
    /// Reads the next message in the buffer, if such a message exists.
    pub fn read(&mut self) -> Option<Message> {
        self.messages.pop_front().map(|message| {
            self.length -= message.len();
            message
        })
    }

    /// Reads messages, where the total length of the returned messages is less than `max_bytes`.
    ///
    /// `Control` messages serve as dividers, and if a control message is encountered no further
    /// messages will be read, even if less than `max_bytes` have been read.
    ///
    /// Any `Packet` messages will be treated as streams, so packets may be split if the entire
    /// packet does not fit within the remaining bytes.
    ///
    /// Returns a vector of read messages, where the sum of the message lengths is guaranteed to be
    /// less than `max_bytes`. The returned `usize` indicates the exact number of bytes read.
    pub fn read_bytes(&mut self, max_bytes: usize) -> (Vec<Message>, usize) {
        let mut number_of_bytes_read = 0;
        let mut messages = vec![];
        while let Some(message) = self.read() {
            match message {
                Message::Control(control) => {
                    // Control messages always get pushed, but then the read is immediately
                    // stopped.
                    messages.push(control.into());
                    break;
                }
                Message::Packet(mut packet) => {
                    let split_packet = packet.split_off(max_bytes - number_of_bytes_read);
                    number_of_bytes_read += packet.len();
                    if !packet.is_empty() {
                        // .. push the part that fit into the result ..
                        messages.push(packet.into());
                    }
                    if !split_packet.is_empty() {
                        // .. and create a new packet for the bytes that did not fit.
                        self.write_front(split_packet.into());
                    }
                    if number_of_bytes_read == max_bytes {
                        // If max_bytes have been read, break the loop.
                        break;
                    }
                }
            };
        }

        (messages, number_of_bytes_read)
    }

    /// Writes a message to the back of the message buffer.
    pub fn write(&mut self, message: Message) {
        self.length += message.len();
        self.messages.push_back(message);
    }

    /// Writes a message to the front of the message buffer.
    fn write_front(&mut self, message: Message) {
        self.length += message.len();
        self.messages.push_front(message);
    }

    /// Returns the total length of all the messages in the message buffer.
    pub fn len(&self) -> usize {
        self.length
    }

    /// Returns true if the message buffer is empty, or contains only empty messages.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Tests that a write followed by a read returns the written message.
    #[test]
    fn test_read_write() {
        let mut message_buffer = MessageBuffer::default();
        let bytes: Vec<u8> = vec![1, 2, 3];
        let message = Message::packet(bytes);
        message_buffer.write(message.clone());
        assert_eq!(message_buffer.len(), 3);
        assert_eq!(message_buffer.read(), Some(message));
        assert!(message_buffer.is_empty());
    }

    /// Tests that multiple writes followed by multiple reads return the data in the correct order.
    #[test]
    fn test_read_write_multiple() {
        let mut message_buffer = MessageBuffer::default();
        let first_bytes: Vec<u8> = vec![1, 2, 3];
        let second_bytes: Vec<u8> = vec![3, 4, 5];

        for message in
            vec![Message::packet(first_bytes.clone()), Message::packet(second_bytes.clone())]
        {
            message_buffer.write(message);
        }

        assert_eq!(message_buffer.len(), first_bytes.len() + second_bytes.len());
        assert_eq!(message_buffer.read(), Some(Message::packet(first_bytes)));
        assert_eq!(message_buffer.len(), second_bytes.len());
        assert_eq!(message_buffer.read(), Some(Message::packet(second_bytes)));
        assert_eq!(message_buffer.read(), None);
    }

    /// Tests that reading 0 bytes returns an empty vector.
    #[test]
    fn test_read_bytes_zero() {
        let mut message_buffer = MessageBuffer::default();
        let bytes: Vec<u8> = vec![1, 2, 3];
        let message = Message::packet(bytes.clone());
        message_buffer.write(message.clone());

        assert_eq!(message_buffer.len(), bytes.len());
        assert_eq!(message_buffer.read_bytes(0), (vec![], 0));
        assert_eq!(message_buffer.len(), bytes.len());
    }

    /// Tests that reading a specific number of bytes that coincides with a message "end" returns
    /// the correct bytes.
    #[test]
    fn test_read_bytes_message_boundary() {
        let mut message_buffer = MessageBuffer::default();
        let first_bytes: Vec<u8> = vec![1, 2];
        let second_bytes: Vec<u8> = vec![3, 4];

        for message in
            vec![Message::packet(first_bytes.clone()), Message::packet(second_bytes.clone())]
        {
            message_buffer.write(message);
        }

        let expected_first_message = Message::packet(first_bytes);
        let expected_second_message = Message::packet(second_bytes);

        assert_eq!(message_buffer.read_bytes(2), (vec![expected_first_message], 2));
        assert_eq!(message_buffer.read_bytes(2), (vec![expected_second_message], 2));
    }

    /// Tests that reading a specific number of bytes that ends in the middle of a `Packet` returns
    /// the expected number of bytes.
    #[test]
    fn test_read_bytes_message_break() {
        let mut message_buffer = MessageBuffer::default();
        let first_bytes: Vec<u8> = vec![1, 2, 3];
        let second_bytes: Vec<u8> = vec![4];

        for message in
            vec![Message::packet(first_bytes.clone()), Message::packet(second_bytes.clone())]
        {
            message_buffer.write(message);
        }

        let expected_first_messages = vec![Message::packet(vec![1, 2])];
        let expected_second_messages = vec![Message::packet(vec![3]), Message::packet(vec![4])];

        assert_eq!(message_buffer.len(), first_bytes.len() + second_bytes.len());
        assert_eq!(message_buffer.read_bytes(2), (expected_first_messages, 2));
        // The first message was split, so verify that the length took the split into account.
        assert_eq!(message_buffer.len(), 2);
        assert_eq!(message_buffer.read_bytes(2), (expected_second_messages, 2));
    }

    /// Tests that attempting to read more bytes than exist in the message buffer returns all the
    /// pending messages.
    #[test]
    fn test_read_bytes_all() {
        let mut message_buffer = MessageBuffer::default();
        let first_bytes: Vec<u8> = vec![1, 2, 3];
        let second_bytes: Vec<u8> = vec![4, 5];
        let third_bytes: Vec<u8> = vec![9, 3];

        for message in vec![
            Message::packet(first_bytes.clone()),
            Message::packet(second_bytes.clone()),
            Message::packet(third_bytes.clone()),
        ] {
            message_buffer.write(message);
        }

        let expected_messages = vec![
            Message::packet(first_bytes),
            Message::packet(second_bytes),
            Message::packet(third_bytes),
        ];

        assert_eq!(message_buffer.read_bytes(100), (expected_messages, 7));
    }

    /// Tests that reading a control message interrupts the byte read, even if more bytes could
    /// have been returned.
    #[test]
    fn test_read_bytes_control_fits() {
        let mut message_buffer = MessageBuffer::default();
        let first_bytes: Vec<u8> = vec![1, 2, 3];
        let control_bytes: Vec<u8> = vec![7, 7, 7];
        let second_bytes: Vec<u8> = vec![4, 5];

        for message in vec![
            Message::packet(first_bytes.clone()),
            Message::control(control_bytes.clone()),
            Message::packet(second_bytes.clone()),
        ] {
            message_buffer.write(message);
        }

        assert_eq!(
            message_buffer.read_bytes(20),
            (vec![Message::packet(first_bytes), Message::control(control_bytes)], 3)
        );
        assert_eq!(message_buffer.read_bytes(2), (vec![Message::packet(second_bytes)], 2));
    }

    /// Tests that the length of the control message is not counted towards the amount of read
    /// bytes, but that the control message is still returned.
    #[test]
    fn test_read_bytes_control_does_not_fit() {
        let mut message_buffer = MessageBuffer::default();
        let first_bytes: Vec<u8> = vec![1, 2, 3];
        let control_bytes: Vec<u8> = vec![7, 7, 7];
        let second_bytes: Vec<u8> = vec![4, 5];

        for message in vec![
            Message::packet(first_bytes.clone()),
            Message::control(control_bytes.clone()),
            Message::packet(second_bytes.clone()),
        ] {
            message_buffer.write(message);
        }

        assert_eq!(
            message_buffer.read_bytes(5),
            (vec![Message::packet(first_bytes), Message::control(control_bytes)], 3)
        );
        assert_eq!(message_buffer.read_bytes(2), (vec![Message::packet(second_bytes)], 2));
    }
}
