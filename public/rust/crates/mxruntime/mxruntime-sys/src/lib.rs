// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Low level bindings for runtime services provided by Zircon

#![allow(non_camel_case_types)]

extern crate fuchsia_zircon_sys as zircon_sys;

use std::os::raw::c_char;

use zircon_sys::zx_handle_t;
use zircon_sys::zx_status_t;

pub const ZX_HANDLE_INVALID: zx_handle_t = 0;

// Startup handle types, derived from zircon/system/public/zircon/processargs.h

// Handle to our own process.
pub const PA_PROC_SELF: u32 = 0x01;
// Handle to the initial thread of our own process.
pub const PA_THREAD_SELF: u32 = 0x02;
// Handle to the VMO containing the ELF image of the system vDSO.  This
// handle is duplicable, transferable, readable, and executable, but not
// writable.  The contents of the VM object should be treated like any
// other general-purpose ELF file image of type ET_DYN.  A process only
// needs this handle so that it can map the vDSO into new processes it
// might create or propagate it on to its children so they can do so.
// Each process's own vDSO was mapped in by its creator before the
// process started, its address passed as an argument to entry point.
pub const PA_VMO_VDSO: u32 = 0x11;
// Handle to the VMO used to map the initial thread's stack.  This
// handle usually has all rights.  The protocol between process creator
// and new process is that this entire VM object has been mapped in
// before the process starts.  The initial value for the SP register in
// the new process is the high edge of the mapping (assuming stacks grow
// downwards), adjusted down as required by the particular machine's C
// calling convention for function entry.  Thus the new process can
// compute its exact stack bounds by subtracting the size reported by
// this VMO from the (adjusted back up) initial SP value.
pub const PA_VMO_STACK: u32 = 0x13;
// Handle to a VMO containing a bootfs format image.
// The "arg" field used with this type is a simple ordinal.
pub const PA_VMO_BOOTFS: u32 = 0x1B;
// Handle to a Job object which can be used to make child processes. The
// Job can be the same as the one used to create this process or it can
// be different.
pub const PA_JOB_DEFAULT: u32 = 0x03;
// Handle types the fdio library uses
pub const PA_FDIO_ROOT: u32 = 0x30;
pub const PA_FDIO_CWD: u32 = 0x31;
pub const PA_FDIO_REMOTE: u32 = 0x32;
pub const PA_FDIO_PIPE: u32 = 0x33;
pub const PA_FDIO_EVENT: u32 = 0x34;
pub const PA_FDIO_LOGGER: u32 = 0x35;
pub const PA_FDIO_SOCKET: u32 = 0x36;
// Used by devmgr and devhosts
pub const PA_RESOURCE: u32 = 0x3F;
// Handle types used by the application model
pub const PA_APP_LAUNCHER: u32 = 0x41;
pub const PA_APP_SERVICES: u32 = 0x43;
// Channel for dynamic loader service
pub const PA_SVC_LOADER: u32 = 0x10;
// VM object handle for the main executable file
pub const PA_VMO_EXECUTABLE: u32 = 0x14;
// Handle types for one-off use and prototyping
pub const PA_USER0: u32 = 0xF0;
pub const PA_USER1: u32 = 0xF1;
pub const PA_USER2: u32 = 0xF2;

#[link(name="fdio")]
extern {
    pub fn zx_get_startup_handle(id: u32) -> zx_handle_t;
    pub fn fdio_service_connect(svcpath: *const c_char, h: zx_handle_t) -> zx_status_t;
    pub fn fdio_service_connect_at(dir: zx_handle_t, path: *const c_char, h: zx_handle_t) -> zx_status_t;
}
