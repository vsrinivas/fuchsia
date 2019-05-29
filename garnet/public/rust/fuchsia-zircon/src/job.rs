// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon jobs.

use crate::ok;
use crate::{object_get_info, ObjectQuery, Topic};
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Process, Status, Task, Vmar};

use fuchsia_zircon_sys as sys;

/// An object representing a Zircon job.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Job(Handle);
impl_handle_based!(Job);

#[repr(C)]
#[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
pub struct JobInfo {
    pub return_code: i64,
    pub exited: bool,
    pub kill_on_oom: bool,
    pub debugger_attached: bool,
}

impl From<sys::zx_info_job_t> for JobInfo {
    fn from(
        sys::zx_info_job_t { return_code, exited, kill_on_oom, debugger_attached }: sys::zx_info_job_t,
    ) -> JobInfo {
        JobInfo { return_code, exited, kill_on_oom, debugger_attached }
    }
}

// JobInfo is able to be safely replaced with a byte representation
unsafe impl ObjectQuery for JobInfo {
    const TOPIC: Topic = Topic::JOB;
    type InfoTy = JobInfo;
}

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
        let status = unsafe {
            sys::zx_process_create(
                parent_job_raw,
                name_ptr,
                name_len,
                options,
                &mut process_out,
                &mut vmar_out,
            )
        };
        ok(status)?;
        unsafe {
            Ok((
                Process::from(Handle::from_raw(process_out)),
                Vmar::from(Handle::from_raw(vmar_out)),
            ))
        }
    }

    /// Wraps the
    /// [zx_object_get_info](https://fuchsia.googlesource.com/fuchsia/+/master/zircon/docs/syscalls/object_get_info.md)
    /// syscall for the ZX_INFO_JOB topic.
    pub fn info(&self) -> Result<JobInfo, Status> {
        let mut info = JobInfo::default();
        object_get_info::<JobInfo>(self.as_handle_ref(), std::slice::from_mut(&mut info))
            .map(|_| info)
    }
}

impl Task for Job {}

#[cfg(test)]
mod tests {
    // The unit tests are built with a different crate name, but fuchsia_runtime returns a "real"
    // fuchsia_zircon::Job that we need to use.
    use fuchsia_zircon::{sys, AsHandleRef, JobInfo, Signals, Task, Time};

    #[test]
    fn info_default() {
        let job = fuchsia_runtime::job_default();
        let info = job.info().unwrap();
        assert_eq!(
            info,
            JobInfo { return_code: 0, exited: false, kill_on_oom: false, debugger_attached: false }
        );
    }

    #[test]
    fn kill_and_info() {
        let default_job = fuchsia_runtime::job_default();
        let job = default_job.create_child_job().expect("Failed to create child job");
        let info = job.info().unwrap();
        assert_eq!(
            info,
            JobInfo { return_code: 0, exited: false, kill_on_oom: false, debugger_attached: false }
        );

        job.kill().expect("Failed to kill job");
        job.wait_handle(Signals::TASK_TERMINATED, Time::INFINITE).unwrap();

        let info = job.info().unwrap();
        assert_eq!(
            info,
            JobInfo {
                return_code: sys::ZX_TASK_RETCODE_SYSCALL_KILL,
                exited: true,
                kill_on_oom: false,
                debugger_attached: false
            }
        );
    }
}
