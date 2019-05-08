// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon jobs.

use crate::{AsHandleRef, HandleBased, Handle, HandleRef, Process, Status, Vmar};
use crate::ok;
use fuchsia_zircon_sys as sys;

/// An object representing a Zircon job.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Job(Handle);
impl_handle_based!(Job);

impl Job {
    /// Create a new job as a child of the current job.
    ///
    /// Wraps the
    /// [zx_job_create](https://fuchsia.googlesource.com/fuchsia/+/master/zircon/docs/syscalls/job_create.md)
    /// syscall.
    pub fn create_child_job(&self) -> Result<Job, Status> {
        let parent_job_raw = self.raw_handle();
        let mut out = 0;
        let options = 0;
        let status = unsafe { sys::zx_job_create(parent_job_raw, options, &mut out) };
        ok(status)?;
        unsafe { Ok(Job::from(Handle::from_raw(out))) }
    }

    /// Create a new process as a child of the current job.
    ///
    /// On success, returns a handle to the new process and a handle to the
    /// root of the new process's address space.
    ///
    /// Wraps the
    /// [zx_process_create](https://fuchsia.googlesource.com/fuchsia/+/master/zircon/docs/syscalls/process_create.md)
    /// syscall.
    pub fn create_child_process(&self, name: &[u8]) -> Result<(Process, Vmar), Status> {
        let parent_job_raw = self.raw_handle();
        let name_ptr = name.as_ptr();
        let name_len = name.len();
        let options = 0;
        let mut process_out = 0;
        let mut vmar_out = 0;
        let status = unsafe { sys::zx_process_create(
            parent_job_raw,
            name_ptr,
            name_len,
            options,
            &mut process_out,
            &mut vmar_out,
        ) };
        ok(status)?;
        unsafe { Ok((
            Process::from(Handle::from_raw(process_out)),
            Vmar::from(Handle::from_raw(vmar_out)),
        )) }
    }
}
