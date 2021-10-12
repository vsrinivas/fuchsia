// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon stream objects.

use {
    crate::{ok, AsHandleRef, Handle, HandleBased, HandleRef, Status, Vmo},
    bitflags::bitflags,
    fuchsia_zircon_sys as sys,
    std::{convert::TryInto, io::SeekFrom},
};

/// An object representing a Zircon [stream](https://fuchsia.dev/fuchsia-src/concepts/objects/stream.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Stream(Handle);
impl_handle_based!(Stream);

bitflags! {
    #[repr(transparent)]
    pub struct StreamOptions: u32 {
        const MODE_READ = sys::ZX_STREAM_MODE_READ;
        const MODE_WRITE = sys::ZX_STREAM_MODE_WRITE;
    }
}

bitflags! {
    #[repr(transparent)]
    pub struct StreamReadOptions: u32 {
        const _RESERVED = 0; // Placeholder until we add some options.
    }
}

bitflags! {
    #[repr(transparent)]
    pub struct StreamWriteOptions: u32 {
        const APPEND = sys::ZX_STREAM_APPEND;
    }
}

impl Stream {
    /// See [zx_stream_create](https://fuchsia.dev/fuchsia-src/reference/syscalls/stream_create)
    pub fn create(options: StreamOptions, vmo: &Vmo, offset: u64) -> Result<Self, Status> {
        let mut handle = 0;
        let status =
            unsafe { sys::zx_stream_create(options.bits(), vmo.raw_handle(), offset, &mut handle) };
        ok(status)?;
        unsafe { Ok(Stream::from(Handle::from_raw(handle))) }
    }

    /// See [zx_stream_readv](https://fuchsia.dev/fuchsia-src/reference/syscalls/stream_readv)
    pub fn readv(
        &self,
        options: StreamReadOptions,
        buffers: &[&mut [u8]],
    ) -> Result<usize, Status> {
        let mut iovec: Vec<_> = buffers
            .iter()
            .map(|b| sys::zx_iovec_t { buffer: b.as_ptr(), capacity: b.len() })
            .collect();
        let mut actual = 0;
        let status = unsafe {
            sys::zx_stream_readv(
                self.raw_handle(),
                options.bits(),
                iovec.as_mut_ptr(),
                iovec.len(),
                &mut actual,
            )
        };
        ok(status)?;
        Ok(actual)
    }

    /// See [zx_stream_readv_at](https://fuchsia.dev/fuchsia-src/reference/syscalls/stream_readv_at)
    pub fn readv_at(
        &self,
        options: StreamReadOptions,
        offset: u64,
        buffers: &[&mut [u8]],
    ) -> Result<usize, Status> {
        let mut iovec: Vec<_> = buffers
            .iter()
            .map(|b| sys::zx_iovec_t { buffer: b.as_ptr(), capacity: b.len() })
            .collect();
        let mut actual = 0;
        let status = unsafe {
            sys::zx_stream_readv_at(
                self.raw_handle(),
                options.bits(),
                offset,
                iovec.as_mut_ptr(),
                iovec.len(),
                &mut actual,
            )
        };
        ok(status)?;
        Ok(actual)
    }

    /// See [zx_stream_seek](https://fuchsia.dev/fuchsia-src/reference/syscalls/stream_seek)
    pub fn seek(&self, pos: SeekFrom) -> Result<u64, Status> {
        let (whence, offset) = match pos {
            SeekFrom::Start(start) => (
                sys::ZX_STREAM_SEEK_ORIGIN_START,
                start.try_into().map_err(|_| Status::OUT_OF_RANGE)?,
            ),
            SeekFrom::End(end) => (sys::ZX_STREAM_SEEK_ORIGIN_END, end),
            SeekFrom::Current(current) => (sys::ZX_STREAM_SEEK_ORIGIN_CURRENT, current),
        };
        let mut pos = 0;
        let status = unsafe { sys::zx_stream_seek(self.raw_handle(), whence, offset, &mut pos) };
        ok(status)?;
        Ok(pos)
    }

    /// See [zx_stream_writev](https://fuchsia.dev/fuchsia-src/reference/syscalls/stream_writev)
    pub fn writev(&self, options: StreamWriteOptions, buffers: &[&[u8]]) -> Result<usize, Status> {
        let iovec: Vec<_> = buffers
            .iter()
            .map(|b| sys::zx_iovec_t { buffer: b.as_ptr(), capacity: b.len() })
            .collect();
        let mut actual = 0;
        let status = unsafe {
            sys::zx_stream_writev(
                self.raw_handle(),
                options.bits(),
                iovec.as_ptr(),
                iovec.len(),
                &mut actual,
            )
        };
        ok(status)?;
        Ok(actual)
    }

    /// See [zx_stream_writev_at](https://fuchsia.dev/fuchsia-src/reference/syscalls/stream_writev_at)
    pub fn writev_at(
        &self,
        options: StreamWriteOptions,
        offset: u64,
        buffers: &[&[u8]],
    ) -> Result<usize, Status> {
        let iovec: Vec<_> = buffers
            .iter()
            .map(|b| sys::zx_iovec_t { buffer: b.as_ptr(), capacity: b.len() })
            .collect();
        let mut actual = 0;
        let status = unsafe {
            sys::zx_stream_writev_at(
                self.raw_handle(),
                options.bits(),
                offset,
                iovec.as_ptr(),
                iovec.len(),
                &mut actual,
            )
        };
        ok(status)?;
        Ok(actual)
    }
}
