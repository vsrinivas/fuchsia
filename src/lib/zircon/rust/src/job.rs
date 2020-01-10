// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon jobs.

use crate::ok;
use crate::{object_get_info, ObjectQuery, Topic};
use crate::{AsHandleRef, Duration, Handle, HandleBased, HandleRef, Process, Status, Task, Vmar};
use bitflags::bitflags;
use std::convert::Into;

use fuchsia_zircon_sys as sys;

/// An object representing a Zircon job.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Job(Handle);
impl_handle_based!(Job);

sys::zx_info_job_t!(JobInfo);

impl From<sys::zx_info_job_t> for JobInfo {
    fn from(info: sys::zx_info_job_t) -> JobInfo {
        let sys::zx_info_job_t { return_code, exited, kill_on_oom, debugger_attached } = info;
        JobInfo { return_code, exited, kill_on_oom, debugger_attached }
    }
}

// JobInfo is able to be safely replaced with a byte representation and is a PoD type.
unsafe impl ObjectQuery for JobInfo {
    const TOPIC: Topic = Topic::JOB;
    type InfoTy = JobInfo;
}

impl Job {
    /// Create a new job as a child of the current job.
    ///
    /// Wraps the
    /// [zx_job_create](https://fuchsia.dev/fuchsia-src/reference/syscalls/job_create.md)
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
    /// [zx_process_create](https://fuchsia.dev/fuchsia-src/reference/syscalls/process_create.md)
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
    /// [zx_object_get_info](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_info.md)
    /// syscall for the ZX_INFO_JOB topic.
    pub fn info(&self) -> Result<JobInfo, Status> {
        let mut info = JobInfo::default();
        object_get_info::<JobInfo>(self.as_handle_ref(), std::slice::from_mut(&mut info))
            .map(|_| info)
    }

    /// Wraps the [zx_job_set_policy](//docs/reference/syscalls/job_set_policy.md) syscall.
    pub fn set_policy(&self, policy: JobPolicy) -> Result<(), Status> {
        match policy {
            JobPolicy::Basic(policy_option, policy_set) => {
                let sys_opt = policy_option.into();
                let sys_topic = sys::ZX_JOB_POL_BASIC;
                let sys_pol: Vec<sys::zx_policy_basic> = policy_set
                    .into_iter()
                    .map(|(condition, action)| sys::zx_policy_basic {
                        condition: condition.into(),
                        policy: action.into(),
                    })
                    .collect();
                let sys_count = sys_pol.len() as u32;

                let sys_pol_ptr = sys_pol.as_ptr();
                ok(unsafe {
                    // No handles or values are moved as a result of this call (regardless of
                    // success), and the values used here are safely dropped when this function
                    // returns.
                    sys::zx_job_set_policy(
                        self.raw_handle(),
                        sys_opt,
                        sys_topic,
                        sys_pol_ptr as *const u8,
                        sys_count,
                    )
                })
            }
            JobPolicy::TimerSlack(min_slack_duration, default_mode) => {
                let sys_opt = sys::ZX_JOB_POL_RELATIVE;
                let sys_topic = sys::ZX_JOB_POL_TIMER_SLACK;
                let sys_pol = sys::zx_policy_timer_slack {
                    min_slack: min_slack_duration.into_nanos(),
                    default_mode: default_mode.into(),
                };
                let sys_count = 1;

                let sys_pol_ptr = &sys_pol as *const sys::zx_policy_timer_slack;
                ok(unsafe {
                    // Requires that `self` contains a currently valid handle.
                    // No handles or values are moved as a result of this call (regardless of
                    // success), and the values used here are safely dropped when this function
                    // returns.
                    sys::zx_job_set_policy(
                        self.raw_handle(),
                        sys_opt,
                        sys_topic,
                        sys_pol_ptr as *const u8,
                        sys_count,
                    )
                })
            }
        }
    }

    /// Wraps the [zx_job_set_critical](//docs/reference/syscalls/job_set_critical.md) syscall.
    pub fn set_critical(&self, opts: JobCriticalOptions, process: &Process) -> Result<(), Status> {
        ok(unsafe {
            sys::zx_job_set_critical(self.raw_handle(), opts.bits(), process.raw_handle())
        })
    }
}

/// Represents the [ZX_JOB_POL_RELATIVE and
/// ZX_JOB_POL_ABSOLUTE](//docs/reference/syscalls/job_set_policy.md) constants
#[derive(Debug, Clone, PartialEq)]
pub enum JobPolicyOption {
    Relative,
    Absolute,
}

impl Into<u32> for JobPolicyOption {
    fn into(self) -> u32 {
        match self {
            JobPolicyOption::Relative => sys::ZX_JOB_POL_RELATIVE,
            JobPolicyOption::Absolute => sys::ZX_JOB_POL_ABSOLUTE,
        }
    }
}

/// Holds a timer policy or a basic policy set for
/// [zx_job_set_policy](//docs/reference/syscalls/job_set_policy.md)
#[derive(Debug, Clone, PartialEq)]
pub enum JobPolicy {
    Basic(JobPolicyOption, Vec<(JobCondition, JobAction)>),
    TimerSlack(Duration, JobDefaultTimerMode),
}

/// Represents the [ZX_POL_*](//docs/reference/syscalls/job_set_policy.md) constants
#[derive(Debug, Clone, PartialEq)]
pub enum JobCondition {
    BadHandle,
    WrongObject,
    VmarWx,
    NewAny,
    NewVmo,
    NewChannel,
    NewEvent,
    NewEventpair,
    NewPort,
    NewSocket,
    NewFifo,
    NewTimer,
    NewProcess,
    NewProfile,
    AmbientMarkVmoExec,
}

impl Into<u32> for JobCondition {
    fn into(self) -> u32 {
        match self {
            JobCondition::BadHandle => sys::ZX_POL_BAD_HANDLE,
            JobCondition::WrongObject => sys::ZX_POL_WRONG_OBJECT,
            JobCondition::VmarWx => sys::ZX_POL_VMAR_WX,
            JobCondition::NewAny => sys::ZX_POL_NEW_ANY,
            JobCondition::NewVmo => sys::ZX_POL_NEW_VMO,
            JobCondition::NewChannel => sys::ZX_POL_NEW_CHANNEL,
            JobCondition::NewEvent => sys::ZX_POL_NEW_EVENT,
            JobCondition::NewEventpair => sys::ZX_POL_NEW_EVENTPAIR,
            JobCondition::NewPort => sys::ZX_POL_NEW_PORT,
            JobCondition::NewSocket => sys::ZX_POL_NEW_SOCKET,
            JobCondition::NewFifo => sys::ZX_POL_NEW_FIFO,
            JobCondition::NewTimer => sys::ZX_POL_NEW_TIMER,
            JobCondition::NewProcess => sys::ZX_POL_NEW_PROCESS,
            JobCondition::NewProfile => sys::ZX_POL_NEW_PROFILE,
            JobCondition::AmbientMarkVmoExec => sys::ZX_POL_AMBIENT_MARK_VMO_EXEC,
        }
    }
}

/// Represents the [ZX_POL_ACTION_*](//docs/reference/syscalls/job_set_policy.md) constants
#[derive(Debug, Clone, PartialEq)]
pub enum JobAction {
    Allow,
    Deny,
    AllowException,
    DenyException,
    Kill,
}

impl Into<u32> for JobAction {
    fn into(self) -> u32 {
        match self {
            JobAction::Allow => sys::ZX_POL_ACTION_ALLOW,
            JobAction::Deny => sys::ZX_POL_ACTION_DENY,
            JobAction::AllowException => sys::ZX_POL_ACTION_ALLOW_EXCEPTION,
            JobAction::DenyException => sys::ZX_POL_ACTION_DENY_EXCEPTION,
            JobAction::Kill => sys::ZX_POL_ACTION_KILL,
        }
    }
}

/// Represents the [ZX_TIMER_SLACK_*](//docs/reference/syscalls/job_set_policy.md) constants
#[derive(Debug, Clone, PartialEq)]
pub enum JobDefaultTimerMode {
    Center,
    Early,
    Late,
}

impl Into<u32> for JobDefaultTimerMode {
    fn into(self) -> u32 {
        match self {
            JobDefaultTimerMode::Center => sys::ZX_TIMER_SLACK_CENTER,
            JobDefaultTimerMode::Early => sys::ZX_TIMER_SLACK_EARLY,
            JobDefaultTimerMode::Late => sys::ZX_TIMER_SLACK_LATE,
        }
    }
}

impl Task for Job {}

bitflags! {
    /// Options that may be used by `Job::set_critical`.
    #[repr(transparent)]
    pub struct JobCriticalOptions: u32 {
        const RETCODE_NONZERO = sys::ZX_JOB_CRITICAL_PROCESS_RETCODE_NONZERO;
    }
}

#[cfg(test)]
mod tests {
    // The unit tests are built with a different crate name, but fuchsia_runtime returns a "real"
    // fuchsia_zircon::Job that we need to use.
    use fuchsia_zircon::{
        sys, AsHandleRef, Duration, JobAction, JobCondition, JobCriticalOptions,
        JobDefaultTimerMode, JobInfo, JobPolicy, JobPolicyOption, Signals, Task, Time,
    };
    use std::ffi::CString;

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

    #[test]
    fn create_and_set_policy() {
        let default_job = fuchsia_runtime::job_default();
        let child_job = default_job.create_child_job().expect("failed to create child job");
        child_job
            .set_policy(JobPolicy::Basic(
                JobPolicyOption::Relative,
                vec![
                    (JobCondition::NewChannel, JobAction::Deny),
                    (JobCondition::NewProcess, JobAction::Allow),
                    (JobCondition::BadHandle, JobAction::Kill),
                ],
            ))
            .expect("failed to set job basic policy");
        child_job
            .set_policy(JobPolicy::TimerSlack(
                Duration::from_millis(10),
                JobDefaultTimerMode::Early,
            ))
            .expect("failed to set job timer slack policy");
    }

    #[test]
    fn create_and_set_critical() {
        let default_job = fuchsia_runtime::job_default();
        let child_job = default_job.create_child_job().expect("failed to create child job");

        let binpath = CString::new("/pkg/bin/sleep_forever_util").unwrap();
        let process =
            fdio::spawn(&child_job, fdio::SpawnOptions::DEFAULT_LOADER, &binpath, &[&binpath])
                .expect("Failed to spawn process");

        child_job
            .set_critical(JobCriticalOptions::RETCODE_NONZERO, &process)
            .expect("failed to set critical process for job");
    }
}
