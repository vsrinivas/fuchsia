// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon event objects.

use crate::{object_get_property, object_set_property};
use crate::{ok, Status};
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Process, Thread};
use crate::{Property, PropertyQuery, PropertyQueryGet, PropertyQuerySet};
use fuchsia_zircon_sys as sys;

/// An object representing a Zircon
/// [exception object](https://fuchsia.dev/fuchsia-src/concepts/kernel/exceptions).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Exception(Handle);
impl_handle_based!(Exception);

impl Exception {
    /// Create a handle for the exception's thread
    ///
    /// Wraps the
    /// [zx_exception_get_thread](https://fuchsia.dev/fuchsia-src/reference/syscalls/exception_get_thread)
    /// syscall.
    pub fn get_thread(&self) -> Result<Thread, Status> {
        let mut handle = 0;
        let status = unsafe { sys::zx_exception_get_thread(self.raw_handle(), &mut handle) };
        ok(status)?;
        unsafe { Ok(Thread::from(Handle::from_raw(handle))) }
    }

    /// Create a handle for the exception's process
    ///
    /// Wraps the
    /// [zx_exception_get_thread](https://fuchsia.dev/fuchsia-src/reference/syscalls/exception_get_thread)
    /// syscall.
    pub fn get_process(&self) -> Result<Process, Status> {
        let mut handle = 0;
        let status = unsafe { sys::zx_exception_get_process(self.raw_handle(), &mut handle) };
        ok(status)?;
        unsafe { Ok(Process::from(Handle::from_raw(handle))) }
    }
}

unsafe_handle_properties!(object: Exception,
    props: [
        {query_ty: EXCEPTION_STATE, tag: ExceptionStateTag, prop_ty: sys::zx_exception_state_t, get: get_exception_state, set: set_exception_state},
    ]
);
