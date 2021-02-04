// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ok;
use crate::{AsHandleRef, Channel, Handle, Status};
use bitflags::bitflags;
use fuchsia_zircon_sys as sys;

bitflags! {
    /// Options that may be used with `Task::create_exception_channel`
    #[repr(transparent)]
    pub struct ExceptionChannelOptions: u32 {
        const DEBUGGER = sys::ZX_EXCEPTION_CHANNEL_DEBUGGER;
    }
}

pub trait Task: AsHandleRef {
    /// Kill the give task (job, process, or thread).
    ///
    /// Wraps the
    /// [zx_task_kill](https://fuchsia.dev/fuchsia-src/reference/syscalls/task_kill.md)
    /// syscall.
    // TODO: Not yet implemented on Thread, need to add object_get_info impl for Thread for proper
    // testability.
    fn kill(&self) -> Result<(), Status> {
        ok(unsafe { sys::zx_task_kill(self.raw_handle()) })
    }

    /// Create an exception channel (with options) for the task.
    ///
    /// Wraps the
    /// [zx_task_create_exception_channel](https://fuchsia.dev/fuchsia-src/reference/syscalls/task_create_exception_channel.md)
    /// syscall.
    fn create_exception_channel_with_opts(
        &self,
        opts: ExceptionChannelOptions,
    ) -> Result<Channel, Status> {
        let mut handle = 0;
        let status = unsafe {
            sys::zx_task_create_exception_channel(self.raw_handle(), opts.bits(), &mut handle)
        };
        ok(status)?;
        unsafe { Ok(Channel::from(Handle::from_raw(handle))) }
    }

    /// Create an exception channel for the task.
    ///
    /// Wraps the
    /// [zx_task_create_exception_channel](https://fuchsia.dev/fuchsia-src/reference/syscalls/task_create_exception_channel.md)
    /// syscall.
    fn create_exception_channel(&self) -> Result<Channel, Status> {
        self.create_exception_channel_with_opts(ExceptionChannelOptions::from_bits_truncate(0))
    }
}
