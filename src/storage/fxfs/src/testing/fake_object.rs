// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_handle::ObjectHandle,
    anyhow::Error,
    async_trait::async_trait,
    std::{
        cmp::min,
        sync::{Arc, Mutex},
        vec::Vec,
    },
};

pub struct FakeObject {
    buf: Vec<u8>,
}

impl FakeObject {
    pub fn new() -> Self {
        FakeObject { buf: Vec::new() }
    }

    pub fn read(&self, offset: u64, buf: &mut [u8]) -> Result<usize, Error> {
        let to_do = min(buf.len(), self.buf.len() - offset as usize);
        buf[0..to_do].copy_from_slice(&self.buf[offset as usize..offset as usize + to_do]);
        Ok(to_do)
    }

    pub fn write(&mut self, offset: u64, buf: &[u8]) -> Result<(), Error> {
        let required_len = offset as usize + buf.len();
        if self.buf.len() < required_len {
            self.buf.resize(required_len, 0);
        }
        &self.buf[offset as usize..offset as usize + buf.len()].copy_from_slice(buf);
        Ok(())
    }

    pub fn get_size(&self) -> u64 {
        self.buf.len() as u64
    }
}

pub struct FakeObjectHandle {
    object: Arc<Mutex<FakeObject>>,
}

impl FakeObjectHandle {
    pub fn new(object: Arc<Mutex<FakeObject>>) -> Self {
        FakeObjectHandle { object }
    }
}

#[async_trait]
impl ObjectHandle for FakeObjectHandle {
    fn object_id(&self) -> u64 {
        0
    }

    async fn read(&self, offset: u64, buf: &mut [u8]) -> Result<usize, Error> {
        self.object.lock().unwrap().read(offset, buf)
    }

    async fn write(&self, offset: u64, buf: &[u8]) -> Result<(), Error> {
        self.object.lock().unwrap().write(offset, buf)
    }

    fn get_size(&self) -> u64 {
        self.object.lock().unwrap().get_size()
    }

    async fn truncate(&self, length: u64) -> Result<(), Error> {
        self.object.lock().unwrap().buf.resize(length as usize, 0);
        Ok(())
    }
}
