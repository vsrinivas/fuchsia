// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Message types and utilities

use std::borrow::{Borrow, BorrowMut};

use byteorder::{ByteOrder, LittleEndian};

use zircon::{Handle, MessageBuf};

/// A buffer for encoding messages.
#[derive(Debug)]
pub struct EncodeBuf {
    buf: Vec<u8>,
    handles: Vec<Handle>,
}

impl EncodeBuf {
    /// Create a new `EncodeBuf`.
    pub fn new() -> Self {
        EncodeBuf {
            buf: Vec::new(),
            handles: Vec::new(),
        }
    }

    /// Clear the buffer, removing all values.
    pub fn clear(&mut self) {
        self.buf.clear();
    }

    /// Extracts a slice containing all of the bytes in the buffer.
    pub fn get_bytes(&self) -> &[u8] {
        &self.buf
    }

    /// Returns a mutable reference to a vector of bytes.
    pub fn get_mut_bytes(&mut self) -> &mut Vec<u8> {
        &mut self.buf
    }

    /// Get the content, in a form suitable for sending to a channel.
    pub fn get_mut_content(&mut self) -> (&[u8], &mut Vec<Handle>) {
        (&self.buf, &mut self.handles)
    }

    /// Create a new request buffer.
    pub fn new_request(ordinal: u32) -> Self {
        let mut buf = Self::new();
        let _ = buf.claim(16);
        LittleEndian::write_u32(buf.get_mut_slice(0, 4), 16);  // size
        LittleEndian::write_u32(buf.get_mut_slice(4, 4), 0);  // version
        LittleEndian::write_u32(buf.get_mut_slice(8, 4), ordinal);
        LittleEndian::write_u32(buf.get_mut_slice(12, 4), 0);  // flags
        buf
    }

    /// Create a new request buffer for a protocol which expects a response.
    pub fn new_request_expecting_response(ordinal: u32) -> Self {
        let mut buf = Self::new();
        let _ = buf.claim(24);
        LittleEndian::write_u32(buf.get_mut_slice(0, 4), 24);  // size
        LittleEndian::write_u32(buf.get_mut_slice(4, 4), 1);  // version
        LittleEndian::write_u32(buf.get_mut_slice(8, 4), ordinal);
        LittleEndian::write_u32(buf.get_mut_slice(12, 4), 1);  // flags
        buf
    }

    /// Create a new response buffer.
    pub fn new_response(ordinal: u32) -> Self {
        let mut buf = Self::new();
        let _ = buf.claim(24);
        LittleEndian::write_u32(buf.get_mut_slice(0, 4), 24);  // size
        LittleEndian::write_u32(buf.get_mut_slice(4, 4), 1);  // version
        LittleEndian::write_u32(buf.get_mut_slice(8, 4), ordinal);
        LittleEndian::write_u32(buf.get_mut_slice(12, 4), 2);  // flags
        buf
    }

    /// Set the id of the current message in the buffer.
    /// This method could panic if the buffer isn't already a proper response message.
    pub fn set_message_id(&mut self, id: u64) {
        LittleEndian::write_u64(self.get_mut_slice(16, 8), id);
    }

    /// Claim a chunk of buffer for writing a new object. Return the offset of the
    /// new chunk relative to the beginning of the buffer.
    pub fn claim(&mut self, size: usize) -> usize {
        let result = self.buf.len();
        self.buf.resize(result + align(size, 8), 0);
        result
    }

    /// Get a mutable slice for a chunk of buffer.
    pub fn get_mut_slice(&mut self, offset: usize, size: usize) -> &mut [u8]
    {
        &mut self.buf[offset .. offset + size]
    }

    /// Encode a pointer, writing it at the given offset.
    pub fn encode_pointer(&mut self, offset: usize) {
        let relative_offset = self.buf.len() - offset;
        LittleEndian::write_u64(self.get_mut_slice(offset, 8), relative_offset as u64);
    }

    /// Encode a handle, returning the index.
    ///
    /// Note: bad things will happen if the caller encodes more than 4G handles.
    pub fn encode_handle(&mut self, handle: Handle) -> u32 {
        let result = self.handles.len() as u32;
        self.handles.push(handle);
        result
    }
}

// We _might_ consider making Decoder a trait so we can use different buffers for it;
// it should be possible to impl Decoder for MessageBuf.

/// A buffer from which fidl messages can be decoded.
#[derive(Debug)]
pub struct DecodeBuf {
    inner: MessageBuf,
    // TODO: state for validation
}

/// The type of the FIDL message.
#[derive(PartialEq, Eq)]
pub enum MsgType {
    /// A request with no response.
    Request = 0,
    /// A request which expects a response.
    RequestExpectsResponse = 1,
    /// A response.
    Response = 2,
}

impl DecodeBuf {
    /// Create a new `DecodeBuf`.
    pub fn new() -> Self {
        DecodeBuf {
            inner: MessageBuf::new(),
        }
    }

    /// Extracts a reference to the inner message buffer.
    pub fn get_buf(&self) -> &MessageBuf {
        &self.inner
    }

    /// Extracts a mutable reference to the inner message buffer.
    pub fn get_mut_buf(&mut self) -> &mut MessageBuf {
        &mut self.inner
    }

    /// Extracts a slice containing all of the bytes in the buffer. 
    pub fn get_bytes(&self) -> &[u8] {
        self.inner.bytes()
    }

    // TODO: ordinal also?
    /// Validate and decode the header of a message
    pub fn decode_message_header(&self) -> Option<MsgType> {
        let buf = self.get_bytes();
        if buf.len() < 16 {
            return None;
        }
        let size = LittleEndian::read_u32(&buf[0..4]);
        let version = LittleEndian::read_u32(&buf[4..8]);
        if version >= 2 || size != 16 + 8 * version || buf.len() < size as usize {
            return None;
        }
        let flags = LittleEndian::read_u32(&buf[12..16]);
        match (flags, version) {
            (0, 0) => Some(MsgType::Request),
            (1, 1) => Some(MsgType::RequestExpectsResponse),
            (2, 1) => Some(MsgType::Response),
            _ => None
        }
    }

    /// Get the ID of the current message in the buffer.
    /// This method may panic if buffer is not a valid message.
    /// Ok if message type is RequestExpectsResponse or Response.
    pub fn get_message_id(&self) -> u64 {
        LittleEndian::read_u64(&self.get_bytes()[16..24])
    }
}

impl Borrow<MessageBuf> for DecodeBuf {
    fn borrow(&self) -> &MessageBuf {
        self.get_buf()
    }
}

impl BorrowMut<MessageBuf> for DecodeBuf {
    fn borrow_mut(&mut self) -> &mut MessageBuf {
        self.get_mut_buf()
    }
}

fn align(offset: usize, alignment: usize) -> usize {
    (offset.wrapping_sub(1) | (alignment - 1)).wrapping_add(1)
}
