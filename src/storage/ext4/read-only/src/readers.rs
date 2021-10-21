// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_mem::Buffer;
use std::{convert::TryInto, sync::Arc};
use thiserror::Error;

#[derive(Error, Debug, PartialEq)]
pub enum ReaderError {
    #[error("Read error at: 0x{:X}", _0)]
    Read(u64),
    #[error("Out of bound read 0x{:X} when size is 0x{:X}", _0, _1)]
    OutOfBounds(u64, u64),
}

pub trait Reader {
    fn read(&self, offset: u64, data: &mut [u8]) -> Result<(), ReaderError>;
}

pub struct VmoReader {
    buffer: Arc<Buffer>,
}

impl Reader for VmoReader {
    fn read(&self, offset: u64, data: &mut [u8]) -> Result<(), ReaderError> {
        let offset_max = offset + data.len() as u64;
        if offset_max > self.buffer.size {
            return Err(ReaderError::OutOfBounds(offset_max, self.buffer.size));
        }
        match self.buffer.vmo.read(data, offset) {
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
    fn read(&self, offset: u64, data: &mut [u8]) -> Result<(), ReaderError> {
        let data_len = data.len() as u64;
        let self_data_len = self.data.len() as u64;
        let offset_max = offset + data_len;
        if offset_max > self_data_len {
            return Err(ReaderError::OutOfBounds(offset_max, self_data_len));
        }

        let offset_for_range: usize = offset.try_into().unwrap();

        match self.data.get(offset_for_range..offset_for_range + data.len()) {
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
