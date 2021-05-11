// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{object_get_info, ok};
use crate::{AsHandleRef, Channel, Handle, ObjectQuery, Status, Topic};
use bitflags::bitflags;
use fuchsia_zircon_sys::{self as sys, zx_duration_t};

bitflags! {
    /// Options that may be used with `Task::create_exception_channel`
    #[repr(transparent)]
    pub struct ExceptionChannelOptions: u32 {
        const DEBUGGER = sys::ZX_EXCEPTION_CHANNEL_DEBUGGER;
    }
}

sys::zx_info_task_runtime_t!(TaskRuntimeInfo);

impl From<sys::zx_info_task_runtime_t> for TaskRuntimeInfo {
    fn from(
        sys::zx_info_task_runtime_t { cpu_time, queue_time, page_fault_time, lock_contention_time }: sys::zx_info_task_runtime_t,
    ) -> TaskRuntimeInfo {
        TaskRuntimeInfo { cpu_time, queue_time, page_fault_time, lock_contention_time }
    }
}

unsafe impl ObjectQuery for TaskRuntimeInfo {
    const TOPIC: Topic = Topic::TASK_RUNTIME;
    type InfoTy = TaskRuntimeInfo;
}

pub trait Task: AsHandleRef {
    /// Kill the given task (job, process, or thread).
    ///
    /// Wraps the
    /// [zx_task_kill](https://fuchsia.dev/fuchsia-src/reference/syscalls/task_kill.md)
    /// syscall.
    // TODO(fxbug.dev/72722): guaranteed to return an error when called on a Thread.
    fn kill(&self) -> Result<(), Status> {
        ok(unsafe { sys::zx_task_kill(self.raw_handle()) })
    }

    /// Suspend the given task
    ///
    /// Wraps the
    /// [zx_task_suspend](https://fuchsia.dev/fuchsia-src/reference/syscalls/task_suspend.md)
    /// syscall.
    ///
    /// Resume the task by closing the returned handle.
    fn suspend(&self) -> Result<Handle, Status> {
        let mut suspend_token = 0;
        let status = unsafe { sys::zx_task_suspend(self.raw_handle(), &mut suspend_token) };
        ok(status)?;
        unsafe { Ok(Handle::from_raw(suspend_token)) }
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

    /// Returns the runtime information for the task.
    ///
    /// Wraps the
    /// [zx_object_get_info]() syscall with `ZX_INFO_TASK_RUNTIME` as the topic.
    fn get_runtime_info(&self) -> Result<TaskRuntimeInfo, Status> {
        let mut info = TaskRuntimeInfo::default();
        object_get_info::<TaskRuntimeInfo>(self.as_handle_ref(), std::slice::from_mut(&mut info))
            .map(|_| info)
    }
}
