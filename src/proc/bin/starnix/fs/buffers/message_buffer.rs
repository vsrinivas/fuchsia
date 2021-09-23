// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::message_types::*;
use crate::task::Task;
use crate::types::*;

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

    /// Returns the next message in the buffer if it is a control message.
    pub fn read_if_control(&mut self) -> Option<Control> {
        match self.messages.pop_front() {
            Some(Message::Control(control)) => Some(control),
            Some(message) => {
                self.messages.push_front(message);
                None
            }
            _ => None,
        }
    }

    /// Reads messages, where the total length of the returned messages is less than `max_bytes`.
    ///
    /// `Control` messages serve as dividers, and if a control message is encountered no further
    /// messages will be read, even if less than `max_bytes` have been read. The control message
    /// itself will not be returned.
    ///
    /// Any `Packet` messages will be treated as streams, so packets may be split if the entire
    /// packet does not fit within the remaining bytes.
    ///
    /// Returns a vector of read messages, where the sum of the message lengths is guaranteed to be
    /// less than `max_bytes`. The returned `usize` indicates the exact number of bytes read.
    pub fn read_packets(&mut self, max_bytes: usize) -> (Vec<Message>, usize) {
        let mut number_of_bytes_read = 0;
        let mut messages = vec![];
        while let Some(message) = self.read() {
            match message {
                Message::Control(control) => {
                    // Control messages are not returned when reading packets, but they do stop the
                    // read from continuing.
                    self.write_front(control.into());
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

    /// Reads `Packet` messages until there are no more messages, a control message is encountered,
    /// or `user_buffers` are full.
    ///
    /// Control messages will stop the read, but will not be written to the user buffers.
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `user_buffers`: The `UserBufferIterator` to write the data to.
    ///
    /// Returns the number of bytes that were read into the buffer, and a control message if one was
    /// read from the socket.
    pub fn read_packets_into_buffer(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
    ) -> Result<usize, Errno> {
        let mut total_bytes_read = 0;

        while !self.is_empty() {
            if let Some(mut user_buffer) = user_buffers.next(usize::MAX) {
                // Try to read enough bytes to fill the current user buffer.
                let (messages, bytes_read) = self.read_packets(user_buffer.length);

                for message in messages {
                    match message {
                        Message::Packet(packet) => {
                            task.mm.write_memory(user_buffer.address, packet.bytes())?;
                            // Update the user address to write to.
                            user_buffer.address += packet.len();
                            total_bytes_read += packet.len();
                        }
                        Message::Control(control) => {
                            // Add the control message back to the message queue. Sockets will
                            // read these explicitly.
                            //
                            // `read_packets` returns early when encountering a control message, so
                            // this will be the last message (which is why there's no need to
                            // explicitly break the loop.
                            self.messages.push_front(control.into());
                        }
                    }
                }

                if bytes_read < user_buffer.length {
                    // If the buffer was not filled, break out of the loop.
                    break;
                }
            } else {
                // Break out of the loop if there is no more space in the user buffers.
                break;
            }
        }

        Ok(total_bytes_read)
    }

    /// Writes a message to the back of the message buffer.
    pub fn write(&mut self, message: Message) {
        self.length += message.len();
        self.messages.push_back(message);
    }

    /// Writes a message to the front of the message buffer.
    pub fn write_front(&mut self, message: Message) {
        self.length += message.len();
        self.messages.push_front(message);
    }

    /// Writes the the contents of `UserBufferIterator` into this socket.
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `user_buffers`: The `UserBufferIterator` to read the data from.
    ///
    /// Returns the number of bytes that were written to the socket.
    pub fn write_buffer(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
    ) -> Result<usize, Errno> {
        let mut bytes_written = 0;
        while let Some(buffer) = user_buffers.next(usize::MAX) {
            let mut bytes = vec![0u8; buffer.length];
            task.mm.read_memory(buffer.address, &mut bytes[..])?;

            bytes_written += bytes.len();
            self.write(Message::packet(bytes));
        }

        Ok(bytes_written)
    }

    /// Returns the total length of all the `Packet` messages in the message buffer.
    ///
    /// This is to simplify the length calculations relative to user buffers (control messages
    /// don't get written back out in the same buffers).
    pub fn len(&self) -> usize {
        self.length
    }

    /// Returns true if the message buffer is empty, or it only contains empty packets. Note that
    /// control messages do not contribute to the length of the message buffer, and thus may
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

    /// Tests that control messages do not contribute to the message buffer length.
    #[test]
    fn test_control_len() {
        let mut message_buffer = MessageBuffer::default();
        let bytes: Vec<u8> = vec![1, 2, 3];
        let message = Message::control(bytes.clone());
        message_buffer.write(message.clone());
        assert_eq!(message_buffer.len(), 0);
        let message = Message::packet(bytes.clone());
        message_buffer.write(message.clone());
        assert_eq!(message_buffer.len(), bytes.len());
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
    fn test_read_packets_zero() {
        let mut message_buffer = MessageBuffer::default();
        let bytes: Vec<u8> = vec![1, 2, 3];
        let message = Message::packet(bytes.clone());
        message_buffer.write(message.clone());

        assert_eq!(message_buffer.len(), bytes.len());
        assert_eq!(message_buffer.read_packets(0), (vec![], 0));
        assert_eq!(message_buffer.len(), bytes.len());
    }

    /// Tests that reading a specific number of bytes that coincides with a message "end" returns
    /// the correct bytes.
    #[test]
    fn test_read_packets_message_boundary() {
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

        assert_eq!(message_buffer.read_packets(2), (vec![expected_first_message], 2));
        assert_eq!(message_buffer.read_packets(2), (vec![expected_second_message], 2));
    }

    /// Tests that reading a specific number of bytes that ends in the middle of a `Packet` returns
    /// the expected number of bytes.
    #[test]
    fn test_read_packets_message_break() {
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
        assert_eq!(message_buffer.read_packets(2), (expected_first_messages, 2));
        // The first message was split, so verify that the length took the split into account.
        assert_eq!(message_buffer.len(), 2);
        assert_eq!(message_buffer.read_packets(2), (expected_second_messages, 2));
    }

    /// Tests that attempting to read more bytes than exist in the message buffer returns all the
    /// pending messages.
    #[test]
    fn test_read_packets_all() {
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

        assert_eq!(message_buffer.read_packets(100), (expected_messages, 7));
    }

    /// Tests that reading a control message interrupts the byte read, even if more bytes could
    /// have been returned.
    #[test]
    fn test_read_packets_control_fits() {
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

        assert_eq!(message_buffer.read_packets(20), (vec![Message::packet(first_bytes)], 3));
        assert_eq!(message_buffer.read_if_control(), Some(control_bytes.into()));
        assert_eq!(message_buffer.read_packets(2), (vec![Message::packet(second_bytes)], 2));
    }

    /// Tests that the length of the control message is not counted towards the amount of read
    /// bytes, and the control message is not returned.
    #[test]
    fn test_read_packets_control_does_not_fit() {
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

        assert_eq!(message_buffer.read_packets(5), (vec![Message::packet(first_bytes)], 3));
        assert_eq!(message_buffer.read_if_control(), Some(control_bytes.into()));
        assert_eq!(message_buffer.read_packets(2), (vec![Message::packet(second_bytes)], 2));
    }
}
