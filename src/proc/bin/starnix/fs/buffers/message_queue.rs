// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::VecDeque;

use super::message_types::*;
use crate::fs::socket::SocketAddress;
use crate::fs::FdEvents;
use crate::task::Task;
use crate::types::*;

#[derive(Debug, Default)]
pub struct MessageReadInfo {
    pub bytes_read: usize,
    pub message_length: usize,
    pub address: Option<SocketAddress>,
    pub ancillary_data: Vec<AncillaryData>,
}

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
    pub fn available_capacity(&self) -> usize {
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

    fn update_address(message: &Message, address: &mut Option<SocketAddress>) -> bool {
        if message.address.is_some() && *address != message.address {
            if address.is_some() {
                return false;
            }
            *address = message.address.clone();
        }
        true
    }

    /// Reads messages until there are no more messages, a message with ancillary data is
    /// encountered, or `user_buffers` are full.
    ///
    /// To read data from the queue without consuming the messages, see `peek_stream`.
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `user_buffers`: The `UserBufferIterator` to write the data to.
    ///
    /// Returns the number of bytes that were read into the buffer, and any ancillary data that was
    /// read.
    pub fn read_stream(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
    ) -> Result<MessageReadInfo, Errno> {
        let mut total_bytes_read = 0;
        let mut address = None;
        let mut ancillary_data = vec![];

        while let Some(mut message) = self.read_message() {
            if !Self::update_address(&message, &mut address) {
                // We've already locked onto an address for this batch of messages, but we
                // have found a message that doesn't match. We put it back for now and
                // return the messages we have so far.
                self.write_front(message);
                break;
            }

            let bytes_read = message.data.copy_to_user(task, user_buffers)?;
            total_bytes_read += bytes_read;

            if let Some(remaining_data) = message.data.split_off(bytes_read) {
                // If not all the message data could fit move the ancillary data to the split off
                // message, so that the ancillary data is returned with the "last" message.
                self.write_front(Message::new(
                    remaining_data,
                    message.address.clone(),
                    message.ancillary_data,
                ));
                break;
            }

            if !message.ancillary_data.is_empty() {
                ancillary_data = message.ancillary_data;
                break;
            }
        }

        Ok(MessageReadInfo {
            bytes_read: total_bytes_read,
            message_length: total_bytes_read,
            address,
            ancillary_data,
        })
    }

    /// Peeks messages until there are no more messages, a message with ancillary data is
    /// encountered, or `user_buffers` are full.
    ///
    /// Unlike `read_stream`, this function does not remove the messages from the queue.
    ///
    /// Used to implement MSG_PEEK.
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `user_buffers`: The `UserBufferIterator` to write the data to.
    ///
    /// Returns the number of bytes that were read into the buffer, and any ancillary data that was
    /// read.
    pub fn peek_stream(
        &self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
    ) -> Result<MessageReadInfo, Errno> {
        let mut total_bytes_read = 0;
        let mut address = None;
        let mut ancillary_data = vec![];

        for message in self.messages.iter() {
            if !Self::update_address(message, &mut address) {
                break;
            }

            let bytes_read = message.data.copy_to_user(task, user_buffers)?;
            total_bytes_read += bytes_read;

            if bytes_read < message.len() {
                break;
            }

            if !message.ancillary_data.is_empty() {
                ancillary_data = message.ancillary_data.clone();
                break;
            }
        }

        Ok(MessageReadInfo {
            bytes_read: total_bytes_read,
            message_length: total_bytes_read,
            address,
            ancillary_data,
        })
    }

    pub fn read_datagram(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
    ) -> Result<MessageReadInfo, Errno> {
        if let Some(message) = self.read_message() {
            Ok(MessageReadInfo {
                bytes_read: message.data.copy_to_user(task, user_buffers)?,
                message_length: message.len(),
                address: message.address,
                ancillary_data: message.ancillary_data,
            })
        } else {
            Ok(MessageReadInfo::default())
        }
    }

    pub fn peek_datagram(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
    ) -> Result<MessageReadInfo, Errno> {
        if let Some(message) = self.peek_message() {
            Ok(MessageReadInfo {
                bytes_read: message.data.copy_to_user(task, user_buffers)?,
                message_length: message.len(),
                address: message.address.clone(),
                ancillary_data: message.ancillary_data.clone(),
            })
        } else {
            Ok(MessageReadInfo::default())
        }
    }

    /// Reads the next message in the buffer, if such a message exists.
    fn read_message(&mut self) -> Option<Message> {
        self.messages.pop_front().map(|message| {
            self.length -= message.len();
            message
        })
    }

    /// Peeks the next message in the buffer, if such a message exists.
    fn peek_message(&self) -> Option<&Message> {
        self.messages.front()
    }

    /// Writes the the contents of `UserBufferIterator` into this socket.
    /// Will return EAGAIN if not enough capacity is available.
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `user_buffers`: The `UserBufferIterator` to read the data from.
    ///
    /// Returns the number of bytes that were written to the socket.
    pub fn write_stream(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
        address: Option<SocketAddress>,
        ancillary_data: &mut Vec<AncillaryData>,
    ) -> Result<usize, Errno> {
        let actual = std::cmp::min(self.available_capacity(), user_buffers.remaining());
        if actual == 0 {
            return error!(EAGAIN);
        }
        let data = MessageData::copy_from_user(task, user_buffers, actual)?;
        self.write_message(Message::new(data, address, std::mem::take(ancillary_data)));
        Ok(actual)
    }

    /// Writes the the contents of `UserBufferIterator` into this socket as
    /// single message. Will return EAGAIN if not enough capacity is available.
    ///
    /// # Parameters
    /// - `task`: The task to read memory from.
    /// - `user_buffers`: The `UserBufferIterator` to read the data from.
    ///
    /// Returns the number of bytes that were written to the socket.
    pub fn write_datagram(
        &mut self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
        address: Option<SocketAddress>,
        ancillary_data: &mut Vec<AncillaryData>,
    ) -> Result<usize, Errno> {
        let actual = user_buffers.remaining();
        if actual > self.capacity() {
            return error!(EMSGSIZE);
        }
        if actual > self.available_capacity() {
            return error!(EAGAIN);
        }
        let data = MessageData::copy_from_user(task, user_buffers, actual)?;
        self.write_message(Message::new(data, address, std::mem::take(ancillary_data)));
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

    pub fn take_messages(&mut self) -> Vec<Message> {
        let mut messages = VecDeque::default();
        std::mem::swap(&mut messages, &mut self.messages);
        self.length = 0;
        messages.into()
    }

    pub fn query_events(&self) -> FdEvents {
        let mut events = FdEvents::empty();
        if self.available_capacity() > 0 {
            events |= FdEvents::POLLOUT;
        }
        if !self.is_empty() {
            events |= FdEvents::POLLIN;
        }
        events
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Tests that a write followed by a read returns the written message.
    #[::fuchsia::test]
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
    #[::fuchsia::test]
    fn test_control_len() {
        let mut message_queue = MessageQueue::new(usize::MAX);
        let bytes: Vec<u8> = vec![1, 2, 3];
        let ancillary_data = vec![AncillaryData::Unix(UnixControlData::Security(bytes.clone()))];
        let message = Message::new(vec![].into(), None, ancillary_data);
        message_queue.write_message(message);
        assert_eq!(message_queue.len(), 0);
        message_queue.write_message(bytes.clone().into());
        assert_eq!(message_queue.len(), bytes.len());
    }

    /// Tests that multiple writes followed by multiple reads return the data in the correct order.
    #[::fuchsia::test]
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
}
