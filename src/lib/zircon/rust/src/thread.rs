// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon threads.

use crate::object_get_info;
use crate::ok;
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Profile, Status, Task};
use crate::{ObjectQuery, Topic};
use fuchsia_zircon_sys as sys;

#[cfg(target_arch = "x86_64")]
use crate::{object_set_property, Property, PropertyQuery, PropertyQuerySet};

/// An object representing a Zircon thread.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Thread(Handle);
impl_handle_based!(Thread);

struct ThreadExceptionReport;
unsafe impl ObjectQuery for ThreadExceptionReport {
    const TOPIC: Topic = Topic::THREAD_EXCEPTION_REPORT;
    type InfoTy = sys::zx_exception_report_t;
}

impl Thread {
    /// Cause the thread to begin execution.
    ///
    /// Wraps the
    /// [zx_thread_start](https://fuchsia.dev/fuchsia-src/reference/syscalls/thread_start.md)
    /// syscall.
    pub fn start(
        &self,
        thread_entry: usize,
        stack: usize,
        arg1: usize,
        arg2: usize,
    ) -> Result<(), Status> {
        let thread_raw = self.raw_handle();
        let status = unsafe { sys::zx_thread_start(thread_raw, thread_entry, stack, arg1, arg2) };
        ok(status)
    }

    /// Apply a scheduling profile to a thread.
    ///
    /// Wraps the
    /// [zx_object_set_profile](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_set_profile) syscall.
    pub fn set_profile(&self, profile: Profile, options: u32) -> Result<(), Status> {
        let thread_raw = self.raw_handle();
        let profile_raw = profile.raw_handle();
        let status = unsafe { sys::zx_object_set_profile(thread_raw, profile_raw, options) };
        ok(status)
    }

    /// Terminate the current running thread.
    ///
    /// Extreme caution should be used-- this is basically always UB in Rust.
    /// There's almost no "normal" program code where this is okay to call.
    /// Users should take care that no references could possibly exist to
    /// stack variables on this thread, and that any destructors, closure
    /// suffixes, or other "after this thing runs" code is waiting to run
    /// in order for safety.
    pub unsafe fn exit() {
        sys::zx_thread_exit()
    }

    /// Wraps the
    /// [zx_object_get_info](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_info.md)
    /// syscall for the ZX_INFO_THREAD_EXCEPTION_REPORT topic.
    pub fn get_exception_report(&self) -> Result<sys::zx_exception_report_t, Status> {
        let mut info: sys::zx_exception_report_t = unsafe { std::mem::zeroed() };
        object_get_info::<ThreadExceptionReport>(
            self.as_handle_ref(),
            std::slice::from_mut(&mut info),
        )
        .map(|_| info)
    }

    pub fn read_state_general_regs(&self) -> Result<sys::zx_thread_state_general_regs_t, Status> {
        let mut state = sys::zx_thread_state_general_regs_t::default();
        let thread_raw = self.raw_handle();
        let status = unsafe {
            sys::zx_thread_read_state(
                thread_raw,
                sys::ZX_THREAD_STATE_GENERAL_REGS,
                &mut state as *mut _ as *mut u8,
                std::mem::size_of_val(&state),
            )
        };
        ok(status).map(|_| state)
    }

    pub fn write_state_general_regs(
        &self,
        state: sys::zx_thread_state_general_regs_t,
    ) -> Result<(), Status> {
        let thread_raw = self.raw_handle();
        let status = unsafe {
            sys::zx_thread_write_state(
                thread_raw,
                sys::ZX_THREAD_STATE_GENERAL_REGS,
                &state as *const _ as *const u8,
                std::mem::size_of_val(&state),
            )
        };
        ok(status)
    }
}

impl Task for Thread {}

#[cfg(target_arch = "x86_64")]
unsafe_handle_properties!(object: Thread,
    props: [
        {query_ty: REGISTER_GS, tag: RegisterGsTag, prop_ty: usize, set: set_register_gs},
        {query_ty: REGISTER_FS, tag: RegisterFsTag, prop_ty: usize, set: set_register_fs},
    ]
);

#[cfg(test)]
mod tests {
    use fuchsia_zircon::{Handle, Profile, Status};

    #[test]
    fn set_profile_invalid() {
        let thread = fuchsia_runtime::thread_self();
        let profile = Profile::from(Handle::invalid());
        assert_eq!(thread.set_profile(profile, 0), Err(Status::BAD_HANDLE));
    }
}
