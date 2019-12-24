// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_mem::Buffer;
use std::sync::Arc;
use thiserror::Error;

#[derive(Error, Debug, PartialEq)]
pub enum ReaderError {
    #[error("Read error at: 0x{:X}", _0)]
    Read(usize),
    #[error("Out of bound read 0x{:X} when size is 0x{:X}", _0, _1)]
    OutOfBounds(usize, usize),
}

pub trait Reader {
    fn read(&self, offset: usize, data: &mut [u8]) -> Result<(), ReaderError>;
}

pub struct VmoReader {
    buffer: Arc<Buffer>,
}

impl Reader for VmoReader {
    fn read(&self, offset: usize, data: &mut [u8]) -> Result<(), ReaderError> {
        let offset_max = offset as usize + data.len();
        if offset_max > self.buffer.size as usize {
            return Err(ReaderError::OutOfBounds(offset_max, self.buffer.size as usize));
        }
        match self.buffer.vmo.read(data, offset as u64) {
            Ok(_) => Ok(()),
            Err(_) => Err(ReaderError::Read(offset)),
        }
    }
}

impl VmoReader {
    pub fn new(filesystem: Arc<Buffer>) -> Self {
        VmoReader { buffer: filesystem }
    }
}

pub struct VecReader {
    data: Vec<u8>,
}

impl Reader for VecReader {
    fn read(&self, offset: usize, data: &mut [u8]) -> Result<(), ReaderError> {
        let offset_max = offset + data.len();
        if offset_max > self.data.len() - 1 {
            return Err(ReaderError::OutOfBounds(offset_max, self.data.len()));
        }
        match self.data.get(offset..offset + data.len()) {
            Some(slice) => {
                data.clone_from_slice(slice);
                Ok(())
            }
            None => Err(ReaderError::Read(offset)),
        }
    }
}

impl VecReader {
    pub fn new(filesystem: Vec<u8>) -> Self {
        VecReader { data: filesystem }
    }
}
