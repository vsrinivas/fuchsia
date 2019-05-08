// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon processes.

use crate::{AsHandleRef, HandleBased, Handle, HandleRef, Status, Thread};
use crate::ok;

use fuchsia_zircon_sys as sys;

/// An object representing a Zircon process.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Process(Handle);
impl_handle_based!(Process);

impl Process {
    /// Similar to `Thread::start`, but is used to start the first thread in a process.
    ///
    /// Wraps the
    /// [zx_process_start](https://fuchsia.googlesource.com/fuchsia/+/master/zircon/docs/syscalls/process_start.md)
    /// syscall.
    pub fn start(
        &self,
        thread: &Thread,
        entry: usize,
        stack: usize,
        arg1: Handle,
        arg2: usize,
    ) -> Result<(), Status> {
        let process_raw = self.raw_handle();
        let thread_raw = thread.raw_handle();
        let arg1 = arg1.into_raw();
        ok(unsafe {
            sys::zx_process_start(
                process_raw, thread_raw, entry, stack, arg1, arg2)
        })
    }

    /// Create a thread inside the current process.
    ///
    /// Wraps the
    /// [zx_thread_create](https://fuchsia.googlesource.com/fuchsia/+/master/zircon/docs/syscalls/thread_create.md)
    /// syscall.
    pub fn create_thread(&self, name: &[u8]) -> Result<Thread, Status> {
        let process_raw = self.raw_handle();
        let name_ptr = name.as_ptr();
        let name_len = name.len();
        let options = 0;
        let mut thread_out = 0;
        let status = unsafe { sys::zx_thread_create(
            process_raw,
            name_ptr,
            name_len,
            options,
            &mut thread_out,
        ) };
        ok(status)?;
        unsafe {
            Ok(Thread::from(Handle::from_raw(thread_out)))
        }
    }
}
