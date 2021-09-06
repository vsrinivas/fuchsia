// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{object_handle::ReadObjectHandle, object_store::data_buffer::DataBuffer},
    anyhow::{bail, Error},
    async_lock::Semaphore,
    async_trait::async_trait,
    fuchsia_zircon::{self as zx},
    once_cell::sync::Lazy,
    std::ops::Range,
};

/// A DataBuffer implementation backed by a VMO.
pub struct VmoDataBuffer(zx::Vmo);

impl VmoDataBuffer {
    pub fn new(size: u64) -> Self {
        let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, size).unwrap();
        vmo.set_content_size(&size).unwrap();
        Self(vmo)
    }

    pub fn vmo(&self) -> &zx::Vmo {
        &self.0
    }
}

impl From<zx::Vmo> for VmoDataBuffer {
    fn from(vmo: zx::Vmo) -> Self {
        Self(vmo)
    }
}

// TODO(fxbug.dev/82412): Because we are using a pager, these system calls are re-entrant but
// they're blocking so they tie up an executor thread.  We could try and workaround this by using
// something like the blocking crate, but that requires making all arguments to the system call
// 'static, which is painful.  It's considerably easier if we just limit the number of threads
// that can be tied up in this way.  It does mean that we must be sure that the paging path
// never passes through here (which should be the case)
static CONCURRENT_SYSCALLS: Lazy<Semaphore> = Lazy::new(|| {
    // The number here *must* be less than the number of threads used for the executor.
    Semaphore::new(4)
});

#[async_trait]
impl DataBuffer for VmoDataBuffer {
    fn raw_read(&self, offset: u64, buf: &mut [u8]) {
        self.0.read(buf, offset).unwrap();
    }

    async fn read(
        &self,
        offset: u64,
        mut buf: &mut [u8],
        _source: &dyn ReadObjectHandle,
    ) -> Result<usize, Error> {
        let size = self.size();
        if offset >= size {
            return Ok(0);
        }
        if size - offset < buf.len() as u64 {
            buf = &mut buf[0..(size - offset) as usize];
        }
        CONCURRENT_SYSCALLS.acquire().await;
        self.0.read(buf, offset)?;
        Ok(buf.len())
    }

    async fn write(
        &self,
        offset: u64,
        buf: &[u8],
        _source: &dyn ReadObjectHandle,
    ) -> Result<(), Error> {
        let _guard = CONCURRENT_SYSCALLS.acquire().await;

        let old_size = self.size();
        let end = offset + buf.len() as u64;

        if end > old_size {
            self.0.set_size(end).unwrap();
        }

        if let Err(e) = self.0.write(buf, offset) {
            self.0.set_size(old_size)?;
            bail!(e);
        }

        Ok(())
    }

    fn size(&self) -> u64 {
        self.0.get_content_size().unwrap()
    }

    async fn resize(&self, size: u64) {
        let _guard = CONCURRENT_SYSCALLS.acquire().await;
        self.0.set_size(size).unwrap();
    }

    fn zero(&self, range: Range<u64>) {
        // TODO(csuter): Is this used, and does it need the CONCURRENT_SYSCALLS guard?
        if range.end == range.start {
            return;
        }
        self.0.op_range(zx::VmoOp::ZERO, range.start, range.end - range.start).unwrap();
    }
}
