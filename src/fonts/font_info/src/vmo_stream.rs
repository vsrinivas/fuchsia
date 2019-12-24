// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(target_os = "fuchsia")]

use {
    crate::sources::FTStreamProvider,
    anyhow::Error,
    freetype_ffi::{FT_Stream, FT_StreamRec},
    fuchsia_zircon as zx,
    libc::{c_uchar, c_ulong, c_void},
    std::{cmp, ptr, slice},
};

struct VmoStreamInternal {
    vmo: zx::Vmo,
    stream_rec: FT_StreamRec,
}

impl VmoStreamInternal {
    /// Caller must ensure that the returned FT_Stream is not used after VmoStream is dropped.
    unsafe fn ft_stream(&self) -> FT_Stream {
        &self.stream_rec as FT_Stream
    }

    fn read(&mut self, offset: u64, read_buffer: &mut [u8]) -> u64 {
        if read_buffer.len() == 0 || offset >= self.stream_rec.size as u64 {
            return 0;
        }
        let read_size = cmp::min(read_buffer.len(), (self.stream_rec.size - offset) as usize);
        match self.vmo.read(&mut read_buffer[..read_size], offset) {
            Ok(_) => read_size as u64,
            Err(err) => {
                println!("Error when reading from font VMO: {:?}", err);
                0
            }
        }
    }

    // Unsafe callback called by freetype to read from the stream.
    unsafe extern "C" fn read_func(
        stream: FT_Stream,
        offset: c_ulong,
        buffer: *mut c_uchar,
        count: c_ulong,
    ) -> c_ulong {
        let wrapper = &mut *((*stream).descriptor as *mut VmoStreamInternal);
        let buffer_slice = slice::from_raw_parts_mut(buffer as *mut u8, count as usize);
        wrapper.read(offset as u64, buffer_slice) as c_ulong
    }

    extern "C" fn close_func(_stream: FT_Stream) {
        // No-op. Stream will be closed when the VmoStream is dropped.
    }
}

/// Implements FT_Stream for a VMO.
pub(crate) struct VmoStream {
    /// VmoStreamInternal needs to be boxed to ensure that it's not moved. This allows to set
    /// `stream_rec.descriptor` to point to the containing `VmoStreamInternal` instance.
    internal: Box<VmoStreamInternal>,
}

impl VmoStream {
    pub fn new(vmo: zx::Vmo, vmo_size: usize) -> Result<VmoStream, Error> {
        let mut internal = Box::new(VmoStreamInternal {
            vmo,
            stream_rec: FT_StreamRec {
                base: ptr::null(),
                size: vmo_size as c_ulong,
                pos: 0,

                descriptor: ptr::null_mut(),
                pathname: ptr::null_mut(),
                read: VmoStreamInternal::read_func,
                close: VmoStreamInternal::close_func,

                memory: ptr::null_mut(),
                cursor: ptr::null_mut(),
                limit: ptr::null_mut(),
            },
        });

        internal.stream_rec.descriptor = &mut *internal as *mut VmoStreamInternal as *mut c_void;

        Ok(VmoStream { internal })
    }

    /// Unsafe to call FreeType FFI.
    /// Caller must ensure that the returned `FT_Stream` is not used after `VmoStream` is dropped.
    pub unsafe fn ft_stream(&self) -> FT_Stream {
        self.internal.ft_stream()
    }
}

impl FTStreamProvider for VmoStream {
    /// Unsafe to call FreeType FFI.
    /// Caller must ensure that the returned `FT_Stream` is not used after `VmoStream` is dropped.
    unsafe fn ft_stream(&self) -> FT_Stream {
        VmoStream::ft_stream(self)
    }
}
