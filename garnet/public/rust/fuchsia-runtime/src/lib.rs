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
    sys::{zx_handle_t, ZX_HANDLE_INVALID}, // handle type (primitive, non-owning)
    Handle,
    Job,
    Process,
    Thread,
    Unowned,
    Vmar,
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
    /// Handle to a job object which can be used to make child processes.
    ///
    /// The job can be the same as the one used to create this process or it can
    /// be different.
    JobDefault = 0x03,

    /// Service for loading shared libraries.
    ///
    /// See `fuchsia.ldsvc.Loader` for the interface definition.
    LdsvcLoader = 0x10,

    /// A handle which will be used as a file descriptor.
    FileDescriptor = 0x30,

    /// Server endpoint for handling connections to appmgr services.
    DirectoryRequest = 0x3B,

    #[doc(hidden)]
    __Nonexhaustive,
}

/// The combination of a [`HandleType`] and an argument.
#[repr(transparent)]
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct HandleId(u32);

impl HandleId {
    /// Creates a handle identifier from a handle type and an argument.
    ///
    /// For example, a `HandleId::create(HandleType::FileDescriptor, 32)` identifies
    /// the handle as file descriptor 32.
    ///
    /// Corresponds to PA_HND in C.
    pub fn create(htype: HandleType, arg: u16) -> Self {
        HandleId(((htype as u32) & 0xFF) | ((arg as u32) << 16))
    }

    /// Convert handle type into a raw u32 for use with the ffi.
    pub fn into_raw(self) -> u32 {
        self.0
    }
}

impl From<HandleType> for HandleId {
    fn from(ty: HandleType) -> Self {
        Self::create(ty, 0)
    }
}

/// Removes the handle of type `HandleType` from the list of handles received at startup.
///
/// This function will return `Some` at-most once per handle type.
/// This function will return `None` if the requested type was not received at
/// startup or if the handle with the provided type was already taken.
pub fn take_startup_handle(htype: HandleId) -> Option<Handle> {
    unsafe {
        let raw = zx_take_startup_handle(htype.0);
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
