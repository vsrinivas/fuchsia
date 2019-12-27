// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon fifo objects.

use crate::ok;
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Status};
use fuchsia_zircon_sys as sys;

/// An object representing a Zircon fifo.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Fifo(Handle);
impl_handle_based!(Fifo);

impl Fifo {
    /// Create a pair of fifos and return their endpoints. Writing to one endpoint enqueues an
    /// element into the fifo from which the opposing endpoint reads. Wraps the
    /// [zx_fifo_create](https://fuchsia.dev/fuchsia-src/reference/syscalls/fifo_create.md)
    /// syscall.
    pub fn create(elem_count: usize, elem_size: usize) -> Result<(Fifo, Fifo), Status> {
        let mut out0 = 0;
        let mut out1 = 0;
        let options = 0;
        let status =
            unsafe { sys::zx_fifo_create(elem_count, elem_size, options, &mut out0, &mut out1) };
        ok(status)?;
        unsafe { Ok((Self::from(Handle::from_raw(out0)), Self::from(Handle::from_raw(out1)))) }
    }

    /// Attempts to write some number of elements into the fifo. The length of `bytes` must be
    /// divisible by `elem_size`, which must match the fifo's element size.
    /// On success, returns the number of elements actually written.
    ///
    /// Wraps
    /// [zx_fifo_write](https://fuchsia.dev/fuchsia-src/reference/syscalls/fifo_write.md).
    pub fn write(&self, elem_size: usize, bytes: &[u8]) -> Result<usize, Status> {
        let count = bytes.len() / elem_size;
        debug_assert!(
            count * elem_size == bytes.len(),
            "bytes.len() must be divisible by elem_size"
        );
        let mut actual_count = 0;
        let status = unsafe {
            sys::zx_fifo_write(
                self.raw_handle(),
                elem_size,
                bytes.as_ptr(),
                count,
                &mut actual_count,
            )
        };
        ok(status).map(|()| actual_count)
    }

    /// Attempts to read some number of elements out of the fifo. The length of `bytes` must be
    /// divisible by `elem_size`, which must match the fifo's element size.
    /// On success, returns the number of elements actually read.
    ///
    /// Wraps
    /// [zx_fifo_read](https://fuchsia.dev/fuchsia-src/reference/syscalls/fifo_read.md).
    pub fn read(&self, elem_size: usize, bytes: &mut [u8]) -> Result<usize, Status> {
        let count = bytes.len() / elem_size;
        debug_assert!(
            count * elem_size == bytes.len(),
            "bytes.len() must be divisible by elem_size"
        );
        let mut actual_count = 0;
        let status = unsafe {
            sys::zx_fifo_read(
                self.raw_handle(),
                elem_size,
                bytes.as_mut_ptr(),
                count,
                &mut actual_count,
            )
        };
        ok(status).map(|()| actual_count)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fifo_basic() {
        let (fifo1, fifo2) = Fifo::create(4, 2).unwrap();

        // Trying to write less than one element should fail.
        assert_eq!(fifo1.write(2, b""), Err(Status::OUT_OF_RANGE));
        // Trying to write using a wrong elem_size should fail
        assert_eq!(fifo1.write(1, b"hi"), Err(Status::OUT_OF_RANGE));

        // Should write one element "he"
        assert_eq!(fifo1.write(2, b"he").unwrap(), 1);

        // Should write three elements "ll" "o " "wo" and drop the rest as it is full.
        assert_eq!(fifo1.write(2, b"llo worlds").unwrap(), 3);

        // Now that the fifo is full any further attempts to write should fail.
        assert_eq!(fifo1.write(2, b"blahblah"), Err(Status::SHOULD_WAIT));

        // Reading with a wrong elem_size should fail
        let mut read_vec = vec![0; 8];
        assert_eq!(fifo2.read(1, &mut read_vec), Err(Status::OUT_OF_RANGE));

        // Read all 4 entries from the other end.
        assert_eq!(fifo2.read(2, &mut read_vec).unwrap(), 4);
        assert_eq!(read_vec, b"hello wo");

        // Reading again should fail as the fifo is empty.
        assert_eq!(fifo2.read(2, &mut read_vec), Err(Status::SHOULD_WAIT));
    }
}
