// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::appendable::{Appendable, BufferTooSmall};

pub mod fake_frames;

pub struct FixedSizedTestBuffer(Vec<u8>);
impl FixedSizedTestBuffer {
    pub fn new(capacity: usize) -> Self {
        Self(Vec::with_capacity(capacity))
    }
}
impl Appendable for FixedSizedTestBuffer {
    fn append_bytes(&mut self, bytes: &[u8]) -> Result<(), BufferTooSmall> {
        if !self.can_append(bytes.len()) {
            return Err(BufferTooSmall);
        }
        self.0.append_bytes(bytes)
    }

    fn append_bytes_zeroed(&mut self, len: usize) -> Result<&mut [u8], BufferTooSmall> {
        if !self.can_append(len) {
            return Err(BufferTooSmall);
        }
        self.0.append_bytes_zeroed(len)
    }

    fn bytes_written(&self) -> usize {
        self.0.bytes_written()
    }

    fn can_append(&self, bytes: usize) -> bool {
        self.0.len() + bytes <= self.0.capacity()
    }
}
