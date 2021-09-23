// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{AsBytes, FromBytes};

use crate::types::*;

/// Matches iovec_t.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, AsBytes, FromBytes)]
#[repr(C)]
pub struct UserBuffer {
    pub address: UserAddress,
    pub length: usize,
}

impl UserBuffer {
    pub fn get_total_length(buffers: &[UserBuffer]) -> usize {
        let mut total = 0;
        for buffer in buffers {
            total += buffer.length;
        }
        total
    }
}
pub struct UserBufferIterator<'a> {
    buffers: &'a [UserBuffer],
    index: usize,
    offset: usize,
}

impl<'a> UserBufferIterator<'a> {
    pub fn new(buffers: &'a [UserBuffer]) -> UserBufferIterator<'a> {
        UserBufferIterator { buffers, index: 0, offset: 0 }
    }

    pub fn remaining(&self) -> usize {
        let remaining =
            self.buffers[self.index..].iter().fold(0, |sum, buffer| sum + buffer.length);
        remaining - self.offset
    }

    pub fn next(&mut self, limit: usize) -> Option<UserBuffer> {
        if self.index >= self.buffers.len() || limit == 0 {
            return None;
        }
        let buffer = &self.buffers[self.index];
        let chunk_size = std::cmp::min(limit, buffer.length - self.offset);
        let result = UserBuffer { address: buffer.address + self.offset, length: chunk_size };
        self.offset += chunk_size;
        while self.index < self.buffers.len() && self.offset == self.buffers[self.index].length {
            self.index += 1;
            self.offset = 0;
        }
        Some(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_user_buffer_iterator() {
        let buffers = vec![
            UserBuffer { address: UserAddress::from_ptr(0x20), length: 7 },
            UserBuffer { address: UserAddress::from_ptr(0x820), length: 0 },
            UserBuffer { address: UserAddress::from_ptr(0x3820), length: 42 },
        ];
        let data = &buffers[..];
        let mut user_buffers = UserBufferIterator::new(data);
        assert_eq!(49, user_buffers.remaining());
        assert_eq!(None, user_buffers.next(0));
        assert_eq!(
            Some(UserBuffer { address: UserAddress::from_ptr(0x20), length: 1 }),
            user_buffers.next(1)
        );
        assert_eq!(48, user_buffers.remaining());
        assert_eq!(
            Some(UserBuffer { address: UserAddress::from_ptr(0x21), length: 2 }),
            user_buffers.next(2)
        );
        assert_eq!(46, user_buffers.remaining());
        assert_eq!(
            Some(UserBuffer { address: UserAddress::from_ptr(0x23), length: 4 }),
            user_buffers.next(31)
        );
        assert_eq!(42, user_buffers.remaining());
        assert_eq!(
            Some(UserBuffer { address: UserAddress::from_ptr(0x3820), length: 9 }),
            user_buffers.next(9)
        );
        assert_eq!(33, user_buffers.remaining());
        assert_eq!(
            Some(UserBuffer { address: UserAddress::from_ptr(0x3829), length: 33 }),
            user_buffers.next(40)
        );
        assert_eq!(0, user_buffers.remaining());
        assert_eq!(None, user_buffers.next(40));
        assert_eq!(0, user_buffers.remaining());
    }
}
