// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::VecDeque;

use super::message_types::*;
use crate::error;
use crate::fs::socket::SocketAddress;
use crate::task::Task;
use crate::types::*;

/// A `MessageQueue` stores a FIFO sequence of messages.
pub struct MessageQueue {
    /// The messages stored in the message queue.
    ///
    /// Writes are added at the end of the queue. Reads consume from the front of the queue.
    messages: VecDeque<Message>,

    /// The total number of bytes currently in the message queue.
    length: usize,

    /// The maximum number of bytes that can be stored inside this pipe.
    capacity: usize,
}

impl MessageQueue {
    pub fn new(capacity: usize) -> MessageQueue {
        MessageQueue { messages: VecDeque::default(), length: 0, capacity }
    }

    /// Returns the number of bytes that can be written to the message queue before the buffer is
    /// full.
    fn available_capacity(&self) -> usize {
        self.capacity - self.length
    }

    /// Returns the total number of bytes this message queue can store, regardless of the current
    /// amount of data in the buffer.
    pub fn capacity(&self) -> usize {
        self.capacity
    }

    /// Sets the capacity of the message queue to the provided number of bytes.
    ///
    /// Reurns an error if the requested capacity could not be set (e.g., if the requested capacity
    /// was less than the current number of bytes stored).
    pub fn set_capacity(&mut self, requested_capacity: usize) -> Result<(), Errno> {
        if requested_capacity < self.length {
            return error!(EBUSY);
        }
        self.capacity = requested_capacity;
        Ok(())
    }

    /// Returns true if the message queue is empty, or it only contains empty messages.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns the total length of all the messages in the message queue.
    pub fn len(&self) -> usize {
        self.length
    }

    /// Reads messages until there are no more messages, a message with ancillary data is
    /// encountered, or `user_buffers` are full.
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `user_buffers`: The `UserBufferIterator` to write the data to.
    ///
    /// Returns the number of bytes that were read into the buffer, and any ancillary data that was
    /// read.
    pub fn read(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
    ) -> Result<(usize, Option<SocketAddress>, Option<AncillaryData>), Errno> {
        let mut total_bytes_read = 0;
        let mut address = None;

        while !self.is_empty() {
            if let Some(mut user_buffer) = user_buffers.next(self.length) {
                // Try to read enough bytes to fill the current user buffer.
                let (messages, bytes_read) = self.read_bytes(&mut address, user_buffer.length);

                for message in messages {
                    task.mm.write_memory(user_buffer.address, message.data.bytes())?;
                    // Update the user address to write to.
                    user_buffer.address += message.len();
                    total_bytes_read += message.len();
                    if message.ancillary_data.is_some() {
                        return Ok((total_bytes_read, address, message.ancillary_data));
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

        Ok((total_bytes_read, address, None))
    }

    /// Reads messages, where the total length of the returned messages is less than `max_bytes`.
    ///
    /// Messages with ancillary data serve as dividers, and if such a message is encountered no more
    /// messages will be read, even if less than `max_bytes` have been read.
    ///
    /// Messages will be treated as streams, so messages may be split if the entire message does not
    /// fit within the remaining bytes.
    ///
    /// Returns a vector of read messages, where the sum of the message lengths is guaranteed to be
    /// less than `max_bytes`. The returned `usize` indicates the exact number of bytes read.
    fn read_bytes(
        &mut self,
        address: &mut Option<SocketAddress>,
        max_bytes: usize,
    ) -> (Vec<Message>, usize) {
        let mut number_of_bytes_read = 0;
        let mut messages = vec![];
        while let Some(mut message) = self.read_message() {
            if message.address.is_some() && *address != message.address {
                if address.is_some() {
                    // We've already locked onto an address for this batch of messages, but we
                    // have found a message that doesn't match. We put it back for now and
                    // return the messages we have so far.
                    self.write_front(message);
                    break;
                }
                *address = message.address.clone();
            }

            // The split_packet contains any bytes that did not fit within the bounds.
            let split_packet = message.data.split_off(max_bytes - number_of_bytes_read);
            number_of_bytes_read += message.len();

            if !split_packet.is_empty() {
                // If not all the message data could fit move the ancillary data to the split off
                // message, so that the ancillary data is returned with the "last" message.
                self.write_front(Message::new(
                    split_packet,
                    message.address.clone(),
                    message.ancillary_data.take(),
                ));
            }

            // Whether or not the message has ancillary data. Note that this needs to be computed
            // after the split of message is pushed, since it might take ownership of the ancillary
            // data.
            let message_has_ancillary_data = message.ancillary_data.is_some();

            if !message.data.is_empty() {
                // If the message data is not empty (i.e., there were still some bytes that could
                // fit), push the message to the result.
                messages.push(message.into());
            }

            if number_of_bytes_read == max_bytes || message_has_ancillary_data {
                // If max_bytes have been read, or there is ancillary data, break the loop.
                break;
            }
        }

        (messages, number_of_bytes_read)
    }

    /// Reads the next message in the buffer, if such a message exists.
    fn read_message(&mut self) -> Option<Message> {
        self.messages.pop_front().map(|message| {
            self.length -= message.len();
            message
        })
    }

    /// Writes the the contents of `UserBufferIterator` into this socket.
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `user_buffers`: The `UserBufferIterator` to read the data from.
    ///
    /// Returns the number of bytes that were written to the socket.
    pub fn write(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
        address: Option<SocketAddress>,
        ancillary_data: &mut Option<AncillaryData>,
    ) -> Result<usize, Errno> {
        let actual = std::cmp::min(self.available_capacity(), user_buffers.remaining());
        let mut bytes = vec![0u8; actual];
        let mut offset = 0;
        while let Some(buffer) = user_buffers.next(actual - offset) {
            task.mm.read_memory(buffer.address, &mut bytes[offset..(offset + buffer.length)])?;
            offset += buffer.length;
        }
        self.write_message(Message::new(bytes.into(), address, ancillary_data.take()));
        Ok(actual)
    }

    /// Writes a message to the front of the message queue.
    fn write_front(&mut self, message: Message) {
        self.length += message.len();
        self.messages.push_front(message);
    }

    /// Writes a message to the back of the message queue.
    pub fn write_message(&mut self, message: Message) {
        self.length += message.len();
        self.messages.push_back(message);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Tests that a write followed by a read returns the written message.
    #[test]
    fn test_read_write() {
        let mut message_queue = MessageQueue::new(usize::MAX);
        let bytes: Vec<u8> = vec![1, 2, 3];
        let message: Message = bytes.into();
        message_queue.write_message(message.clone());
        assert_eq!(message_queue.len(), 3);
        assert_eq!(message_queue.read_message(), Some(message));
        assert!(message_queue.is_empty());
    }

    /// Tests that ancillary data does not contribute to the message queue length.
    #[test]
    fn test_control_len() {
        let mut message_queue = MessageQueue::new(usize::MAX);
        let bytes: Vec<u8> = vec![1, 2, 3];
        let message = Message::new(vec![].into(), None, Some(bytes.clone().into()));
        message_queue.write_message(message.clone());
        assert_eq!(message_queue.len(), 0);
        message_queue.write_message(bytes.clone().into());
        assert_eq!(message_queue.len(), bytes.len());
    }

    /// Tests that multiple writes followed by multiple reads return the data in the correct order.
    #[test]
    fn test_read_write_multiple() {
        let mut message_queue = MessageQueue::new(usize::MAX);
        let first_bytes: Vec<u8> = vec![1, 2, 3];
        let second_bytes: Vec<u8> = vec![3, 4, 5];

        for message in vec![first_bytes.clone().into(), second_bytes.clone().into()] {
            message_queue.write_message(message);
        }

        assert_eq!(message_queue.len(), first_bytes.len() + second_bytes.len());
        assert_eq!(message_queue.read_message(), Some(first_bytes.into()));
        assert_eq!(message_queue.len(), second_bytes.len());
        assert_eq!(message_queue.read_message(), Some(second_bytes.into()));
        assert_eq!(message_queue.read_message(), None);
    }

    /// Tests that reading 0 bytes returns an empty vector.
    #[test]
    fn test_read_bytes_zero() {
        let mut message_queue = MessageQueue::new(usize::MAX);
        let bytes: Vec<u8> = vec![1, 2, 3];
        message_queue.write_message(bytes.clone().into());

        assert_eq!(message_queue.len(), bytes.len());
        assert_eq!(message_queue.read_bytes(&mut None, 0), (vec![], 0));
        assert_eq!(message_queue.len(), bytes.len());
    }

    /// Tests that reading a specific number of bytes that coincides with a message "end" returns
    /// the correct bytes.
    #[test]
    fn test_read_bytes_message_boundary() {
        let mut message_queue = MessageQueue::new(usize::MAX);
        let first_bytes: Vec<u8> = vec![1, 2];
        let second_bytes: Vec<u8> = vec![3, 4];

        for message in vec![first_bytes.clone().into(), second_bytes.clone().into()] {
            message_queue.write_message(message);
        }

        let expected_first_message = first_bytes.into();
        let expected_second_message = second_bytes.into();

        assert_eq!(message_queue.read_bytes(&mut None, 2), (vec![expected_first_message], 2));
        assert_eq!(message_queue.read_bytes(&mut None, 2), (vec![expected_second_message], 2));
    }

    /// Tests that reading a specific number of bytes that ends in the middle of a message returns
    /// the expected number of bytes.
    #[test]
    fn test_read_bytes_message_break() {
        let mut message_queue = MessageQueue::new(usize::MAX);
        let first_bytes: Vec<u8> = vec![1, 2, 3];
        let second_bytes: Vec<u8> = vec![4];

        for message in vec![first_bytes.clone().into(), second_bytes.clone().into()] {
            message_queue.write_message(message);
        }

        let expected_first_messages = vec![vec![1, 2].into()];
        let expected_second_messages = vec![vec![3].into(), vec![4].into()];

        assert_eq!(message_queue.len(), first_bytes.len() + second_bytes.len());
        assert_eq!(message_queue.read_bytes(&mut None, 2), (expected_first_messages, 2));
        // The first message was split, so verify that the length took the split into account.
        assert_eq!(message_queue.len(), 2);
        assert_eq!(message_queue.read_bytes(&mut None, 2), (expected_second_messages, 2));
    }

    /// Tests that attempting to read more bytes than exist in the message queue returns all the
    /// pending messages.
    #[test]
    fn test_read_bytes_all() {
        let mut message_queue = MessageQueue::new(usize::MAX);
        let first_bytes: Vec<u8> = vec![1, 2, 3];
        let second_bytes: Vec<u8> = vec![4, 5];
        let third_bytes: Vec<u8> = vec![9, 3];

        for message in vec![
            first_bytes.clone().into(),
            second_bytes.clone().into(),
            third_bytes.clone().into(),
        ] {
            message_queue.write_message(message);
        }

        let expected_messages = vec![first_bytes.into(), second_bytes.into(), third_bytes.into()];

        assert_eq!(message_queue.read_bytes(&mut None, 100), (expected_messages, 7));
    }

    /// Tests that reading a control message interrupts the byte read, even if more bytes could
    /// have been returned.
    #[test]
    fn test_read_bytes_control_fits() {
        let mut message_queue = MessageQueue::new(usize::MAX);
        let first_bytes: Vec<u8> = vec![1, 2, 3];
        let control_bytes: Vec<u8> = vec![7, 7, 7];
        let second_bytes: Vec<u8> = vec![4, 5];

        for message in vec![
            Message::new(first_bytes.clone().into(), None, Some(control_bytes.clone().into())),
            second_bytes.clone().into(),
        ] {
            message_queue.write_message(message);
        }

        let (messages, bytes) = message_queue.read_bytes(&mut None, 20);
        assert_eq!(
            messages,
            vec![Message::new(first_bytes.into(), None, Some(control_bytes.into()))]
        );
        assert_eq!(bytes, 3);
        assert_eq!(message_queue.read_bytes(&mut None, 2), (vec![second_bytes.into()], 2));
    }

    /// Tests that the length of the control message is not counted towards the amount of read
    /// bytes.
    #[test]
    fn test_read_bytes_control_does_not_fit() {
        let mut message_queue = MessageQueue::new(usize::MAX);
        let first_bytes: Vec<u8> = vec![1, 2, 3];
        let control_bytes: Vec<u8> = vec![7, 7, 7];
        let second_bytes: Vec<u8> = vec![4, 5];

        for message in vec![
            Message::new(first_bytes.clone().into(), None, Some(control_bytes.clone().into())),
            second_bytes.clone().into(),
        ] {
            message_queue.write_message(message);
        }

        let (messages, bytes) = message_queue.read_bytes(&mut None, 5);
        assert_eq!(
            messages,
            vec![Message::new(first_bytes.into(), None, Some(control_bytes.into()))]
        );
        assert_eq!(bytes, 3);
        assert_eq!(message_queue.read_bytes(&mut None, 2), (vec![second_bytes.into()], 2));
    }

    /// Tests that ancillary data is returned with the "second" part of a split message.
    #[test]
    fn test_read_bytes_control_split() {
        let mut message_queue = MessageQueue::new(usize::MAX);
        let first_bytes: Vec<u8> = vec![1, 2, 3];
        let control_bytes: Vec<u8> = vec![7, 7, 7];
        let second_bytes: Vec<u8> = vec![4, 5];

        for message in vec![
            Message::new(first_bytes.clone().into(), None, Some(control_bytes.clone().into())),
            second_bytes.clone().into(),
        ] {
            message_queue.write_message(message);
        }

        // The first_bytes won't fit here, so the ancillary data should not have been returned.
        assert_eq!(message_queue.read_bytes(&mut None, 2), (vec![vec![1, 2].into()], 2));
        // One byte remains from the first message, and the ancillary data should be included.
        assert_eq!(
            message_queue.read_bytes(&mut None, 2),
            (vec![Message::new(vec![3].into(), None, Some(control_bytes.into()))], 1)
        );
        assert_eq!(message_queue.read_bytes(&mut None, 2), (vec![second_bytes.into()], 2));
    }

    #[test]
    fn test_read_bytes_address_boundary() {
        let mut message_queue = MessageQueue::new(usize::MAX);
        let messages = vec![
            Message::new(vec![1, 2, 3].into(), Some(SocketAddress::Unix(b"/foo".to_vec())), None),
            Message::new(vec![4, 5].into(), None, None),
            Message::new(vec![6, 7, 8].into(), Some(SocketAddress::Unix(b"/foo".to_vec())), None),
            Message::new(vec![9, 10, 11].into(), Some(SocketAddress::Unix(b"/bar".to_vec())), None),
            Message::new(vec![12].into(), Some(SocketAddress::Unix(b"/bar".to_vec())), None),
        ];

        for message in messages.clone().into_iter() {
            message_queue.write_message(message);
        }

        let mut address = None;
        assert_eq!(
            message_queue.read_bytes(&mut address, 20),
            (vec![messages[0].clone(), messages[1].clone(), messages[2].clone()], 8)
        );
        assert_eq!(address, Some(SocketAddress::Unix(b"/foo".to_vec())));
        let mut address = None;
        assert_eq!(
            message_queue.read_bytes(&mut address, 20),
            (vec![messages[3].clone(), messages[4].clone()], 4)
        );
        assert_eq!(address, Some(SocketAddress::Unix(b"/bar".to_vec())));
    }
}
