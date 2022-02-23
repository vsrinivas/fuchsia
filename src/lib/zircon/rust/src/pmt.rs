// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon pmt objects.

use crate::ok;
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Status};
use fuchsia_zircon_sys as sys;
use std::mem;

/// An object representing a Zircon Pinned Memory Token.
/// See [PMT Documentation](https://fuchsia.dev/fuchsia-src/reference/kernel_objects/pinned_memory_token) for details.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Pmt(Handle);
impl_handle_based!(Pmt);

impl Pmt {
    // Unpins a Pinned Memory Token.
    // Wraps the
    // [`zx_pmt_unpin`](https://fuchsia.dev/fuchsia-src/reference/syscalls/pmt_unpin) system call to unpin a pmt.
    pub fn unpin(self) -> Result<(), Status> {
        let status = unsafe { sys::zx_pmt_unpin(self.raw_handle()) };

        // According to the syscall documentation, after calling `zx_pmt_unpin`,
        // the pmt handle cannot be used anymore, not even for `zx_handle_close`.
        // This is also true even if the function returns an error.
        mem::forget(self);

        ok(status)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Handle, Vmo};

    #[test]
    fn pmt_unpin_invalid_handle() {
        let pmt = Pmt::from(Handle::invalid());
        let status = pmt.unpin();
        assert_eq!(status, Err(Status::BAD_HANDLE));
    }

    #[test]
    fn pmt_unpin_wrong_handle() {
        let vmo = Vmo::create(0).unwrap();
        let pmt = unsafe { Pmt::from(Handle::from_raw(vmo.into_raw())) };

        let status = pmt.unpin();
        assert_eq!(status, Err(Status::WRONG_TYPE));
    }
}
