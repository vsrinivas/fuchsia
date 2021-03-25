// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        device::{
            buffer::{Buffer, BufferRef, MutableBufferRef},
            buffer_allocator::{BufferAllocator, MemBufferSource},
        },
        object_handle::ObjectHandle,
    },
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

    pub fn read(&self, offset: u64, mut buf: MutableBufferRef<'_>) -> Result<usize, Error> {
        let to_do = min(buf.len(), self.buf.len() - offset as usize);
        buf.as_mut_slice()[0..to_do]
            .copy_from_slice(&self.buf[offset as usize..offset as usize + to_do]);
        Ok(to_do)
    }

    pub fn write(&mut self, offset: u64, buf: BufferRef<'_>) -> Result<(), Error> {
        let required_len = offset as usize + buf.len();
        if self.buf.len() < required_len {
            self.buf.resize(required_len, 0);
        }
        &self.buf[offset as usize..offset as usize + buf.len()].copy_from_slice(buf.as_slice());
        Ok(())
    }

    pub fn get_size(&self) -> u64 {
        self.buf.len() as u64
    }
}

pub struct FakeObjectHandle {
    object: Arc<Mutex<FakeObject>>,
    allocator: BufferAllocator,
}

impl FakeObjectHandle {
    pub fn new(object: Arc<Mutex<FakeObject>>) -> Self {
        // TODO(jfsulliv): Should this take an allocator as parameter?
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(32 * 1024 * 1024)));
        FakeObjectHandle { object, allocator }
    }
}

#[async_trait]
impl ObjectHandle for FakeObjectHandle {
    fn object_id(&self) -> u64 {
        0
    }

    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        self.allocator.allocate_buffer(size)
    }

    async fn read(&self, offset: u64, buf: MutableBufferRef<'_>) -> Result<usize, Error> {
        self.object.lock().unwrap().read(offset, buf)
    }

    async fn write(&self, offset: u64, buf: BufferRef<'_>) -> Result<(), Error> {
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
