// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Fuchsia-specific `libc` functionality.
//!
//! This crate is a minimal extension on top of the `fuchsia-zircon` crate,
//! which provides bindings to the Zircon kernel's syscalls, but does not
//! depend on functionality from `libc`.

// AKA `libc`-granted ambient-authority crate ;)

#![deny(missing_docs)]

use fuchsia_zircon::{
    Unowned, // non-owning wrapper for handle types
    Handle, Job, Process, Thread, Vmar, // handle types
    sys::{zx_handle_t, ZX_HANDLE_INVALID}, // handle type (primitive, non-owning)
};

extern "C" {
    pub fn zx_take_startup_handle(hnd_info: u32) -> zx_handle_t;
    pub fn zx_thread_self() -> zx_handle_t;
    pub fn zx_process_self() -> zx_handle_t;
    pub fn zx_vmar_root_self() -> zx_handle_t;
    pub fn zx_job_default() -> zx_handle_t;
}

/// Startup handle types, derived from zircon/system/public/zircon/processargs.h.
/// Other variants may be added in the future.
#[repr(u32)]
pub enum HandleType {
    /// Server endpoint for handling connections to appmgr services.
    DirectoryRequest = 0x3B,

    #[doc(hidden)]
    __Nonexhaustive,
}

/// Removes the handle of type `HandleType` from the list of handles received at startup.
///
/// This function will return `Some` at-most once per handle type.
/// This function will return `None` if the requested type was not received at
/// startup or if the handle with the provided type was already taken.
pub fn take_startup_handle(htype: HandleType) -> Option<Handle> {
    unsafe {
        let raw = zx_take_startup_handle(htype as u32);
        if raw == ZX_HANDLE_INVALID {
            None
        } else {
            Some(Handle::from_raw(raw))
        }
    }
}

/// Get a reference to the handle of the current thread.
pub fn thread_self() -> Unowned<'static, Thread> {
    unsafe {
        let handle = zx_thread_self();
        Unowned::from_raw_handle(handle)
    }
}

/// Get a reference to the handle of the current process.
pub fn process_self() -> Unowned<'static, Process> {
    unsafe {
        let handle = zx_process_self();
        Unowned::from_raw_handle(handle)
    }
}

/// Get a reference to the handle of the current address space.
pub fn vmar_root_self() -> Unowned<'static, Vmar> {
    unsafe {
        let handle = zx_vmar_root_self();
        Unowned::from_raw_handle(handle)
    }
}

/// Get a reference to the default `Job` provided to the process on startup.
///
/// This typically refers to the `Job` that is the immediate parent of the current
/// process.
///
/// If the current process was launched as a Fuchsia Component, this `Job`
/// will begin with no child processes other than the current process.
pub fn job_default() -> Unowned<'static, Job> {
    unsafe {
        let handle = zx_job_default();
        Unowned::from_raw_handle(handle)
    }
}
