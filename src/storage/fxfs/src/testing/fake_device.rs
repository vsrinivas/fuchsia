// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::object_store, anyhow::Error, std::sync::Mutex};

pub struct FakeDevice {
    block_size: u64,
    data: Mutex<Vec<u8>>,
}

impl FakeDevice {
    pub fn new(block_size: u64) -> Self {
        Self { block_size, data: Mutex::new(Vec::new()) }
    }
}

impl object_store::device::Device for FakeDevice {
    fn block_size(&self) -> u64 {
        self.block_size
    }
    fn read(&self, offset: u64, buf: &mut [u8]) -> Result<(), Error> {
        assert!(offset % self.block_size == 0);
        assert!(buf.len() % self.block_size as usize == 0);
        let required_len = offset as usize + buf.len();
        let data = self.data.lock().unwrap();
        assert!(data.len() >= required_len);
        buf.copy_from_slice(&data[offset as usize..offset as usize + buf.len()]);
        Ok(())
    }

    fn write(&self, offset: u64, buf: &[u8]) -> Result<(), Error> {
        assert!(buf.len() % self.block_size as usize == 0);
        let end = offset as usize + buf.len();
        let mut data = self.data.lock().unwrap();
        if data.len() < end {
            data.resize(end, 0);
        }
        data[offset as usize..end].copy_from_slice(buf);
        Ok(())
    }
}
