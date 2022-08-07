// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{data_buffer::DataBuffer, object_handle::ReadObjectHandle},
    anyhow::Error,
    async_lock::Semaphore,
    async_trait::async_trait,
    fuchsia_zircon::{self as zx},
    once_cell::sync::Lazy,
    std::convert::{TryFrom, TryInto},
};

/// A DataBuffer implementation backed by a VMO.
pub struct VmoDataBuffer {
    vmo: zx::Vmo,
    stream: zx::Stream,
}

impl VmoDataBuffer {
    pub fn new(size: u64) -> Self {
        let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, size).unwrap();
        vmo.set_content_size(&size).unwrap();
        vmo.try_into().unwrap()
    }

    pub fn vmo(&self) -> &zx::Vmo {
        &self.vmo
    }
}

impl TryFrom<zx::Vmo> for VmoDataBuffer {
    type Error = Error;

    fn try_from(vmo: zx::Vmo) -> Result<Self, Error> {
        let stream = zx::Stream::create(
            zx::StreamOptions::MODE_READ | zx::StreamOptions::MODE_WRITE,
            &vmo,
            0,
        )?;
        Ok(Self { vmo, stream })
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
        self.vmo.read(buf, offset).unwrap();
    }

    async fn read(
        &self,
        offset: u64,
        buf: &mut [u8],
        _source: &dyn ReadObjectHandle,
    ) -> Result<usize, Error> {
        let _guard = CONCURRENT_SYSCALLS.acquire().await;
        Ok(self.stream.readv_at(zx::StreamReadOptions::empty(), offset, &[buf])?)
    }

    async fn write(
        &self,
        offset: u64,
        buf: &[u8],
        _source: &dyn ReadObjectHandle,
    ) -> Result<(), Error> {
        let _guard = CONCURRENT_SYSCALLS.acquire().await;

        self.stream
            .writev_at(zx::StreamWriteOptions::empty(), offset, &[buf])
            .expect("write failed");

        Ok(())
    }

    fn size(&self) -> u64 {
        self.vmo.get_content_size().unwrap()
    }

    async fn resize(&self, size: u64) {
        let _guard = CONCURRENT_SYSCALLS.acquire().await;
        let old_vmo_size = self.vmo.get_size().unwrap();
        if size > old_vmo_size {
            self.vmo.set_size(size).unwrap();
        } else {
            self.vmo.set_content_size(&size).unwrap();
        }
    }
}
