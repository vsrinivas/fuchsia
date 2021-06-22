// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{BlockClient, BufferSlice, MutableBufferSlice, VmoId},
    anyhow::{anyhow, ensure, Error},
    async_trait::async_trait,
    fuchsia_zircon as zx,
    std::{
        collections::BTreeMap,
        sync::{
            atomic::{self, AtomicU32},
            Mutex,
        },
    },
};

type VmoRegistry = BTreeMap<u16, zx::Vmo>;

struct Inner {
    data: Vec<u8>,
    vmo_registry: VmoRegistry,
}

/// A fake instance of BlockClient for use in tests.
pub struct FakeBlockClient {
    inner: Mutex<Inner>,
    block_size: u32,
    flush_count: AtomicU32,
}

impl FakeBlockClient {
    pub fn new(block_size: u32, block_count: usize) -> Self {
        Self {
            inner: Mutex::new(Inner {
                data: vec![0 as u8; block_size as usize * block_count],
                vmo_registry: BTreeMap::new(),
            }),
            block_size,
            flush_count: AtomicU32::new(0),
        }
    }

    pub fn flush_count(&self) -> u32 {
        self.flush_count.load(atomic::Ordering::Relaxed)
    }
}

#[async_trait]
impl BlockClient for FakeBlockClient {
    async fn attach_vmo(&self, vmo: &zx::Vmo) -> Result<VmoId, Error> {
        let len = vmo.get_size()?;
        let vmo = vmo.create_child(zx::VmoChildOptions::SLICE, 0, len)?;
        let mut inner = self.inner.lock().unwrap();
        // 0 is a sentinel value
        for id in 1..u16::MAX {
            if !inner.vmo_registry.contains_key(&id) {
                inner.vmo_registry.insert(id, vmo);
                return Ok(VmoId::new(id));
            }
        }
        Err(anyhow!("Out of vmoids"))
    }

    async fn detach_vmo(&self, vmo_id: VmoId) -> Result<(), Error> {
        let mut inner = self.inner.lock().unwrap();
        let id = vmo_id.into_id();
        if let None = inner.vmo_registry.remove(&id) {
            Err(anyhow!("Removed nonexistent vmoid {}", id))
        } else {
            Ok(())
        }
    }

    async fn read_at(
        &self,
        buffer_slice: MutableBufferSlice<'_>,
        device_offset: u64,
    ) -> Result<(), Error> {
        ensure!(device_offset % self.block_size as u64 == 0, "bad alignment");
        let device_offset = device_offset as usize;
        let inner = &mut *self.inner.lock().unwrap();
        match buffer_slice {
            MutableBufferSlice::VmoId { vmo_id, offset, length } => {
                ensure!(offset % self.block_size as u64 == 0, "Bad alignment");
                ensure!(length % self.block_size as u64 == 0, "Bad alignment");
                let vmo = inner
                    .vmo_registry
                    .get(&vmo_id.id())
                    .ok_or(anyhow!("Invalid vmoid {:?}", vmo_id))?;
                vmo.write(&inner.data[device_offset..device_offset + length as usize], offset)?;
                Ok(())
            }
            MutableBufferSlice::Memory(slice) => {
                let len = slice.len();
                ensure!(device_offset + len <= inner.data.len(), "Invalid range");
                slice.copy_from_slice(&inner.data[device_offset..device_offset + len]);
                Ok(())
            }
        }
    }

    async fn write_at(
        &self,
        buffer_slice: BufferSlice<'_>,
        device_offset: u64,
    ) -> Result<(), Error> {
        ensure!(device_offset % self.block_size as u64 == 0, "bad alignment");
        let device_offset = device_offset as usize;
        let inner = &mut *self.inner.lock().unwrap();
        match buffer_slice {
            BufferSlice::VmoId { vmo_id, offset, length } => {
                ensure!(offset % self.block_size as u64 == 0, "Bad alignment");
                ensure!(length % self.block_size as u64 == 0, "Bad alignment");
                let vmo = inner
                    .vmo_registry
                    .get(&vmo_id.id())
                    .ok_or(anyhow!("Invalid vmoid {:?}", vmo_id))?;
                vmo.read(&mut inner.data[device_offset..device_offset + length as usize], offset)?;
                Ok(())
            }
            BufferSlice::Memory(slice) => {
                let len = slice.len();
                ensure!(device_offset + len <= inner.data.len(), "Invalid range");
                inner.data[device_offset..device_offset + len].copy_from_slice(slice);
                Ok(())
            }
        }
    }

    async fn flush(&self) -> Result<(), Error> {
        self.flush_count.fetch_add(1, atomic::Ordering::Relaxed);
        Ok(())
    }

    async fn close(&self) -> Result<(), Error> {
        Ok(())
    }

    fn block_size(&self) -> u32 {
        self.block_size
    }

    fn block_count(&self) -> u64 {
        self.inner.lock().unwrap().data.len() as u64 / self.block_size as u64
    }

    fn is_connected(&self) -> bool {
        true
    }
}
