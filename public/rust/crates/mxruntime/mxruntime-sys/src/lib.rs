// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Low level bindings for runtime services provided by Magenta

#![allow(non_camel_case_types)]

extern crate magenta_sys;

use magenta_sys::mx_handle_t;

pub const MX_HANDLE_INVALID: mx_handle_t = 0;

// Startup handle types, derived from magenta/system/public/magenta/processargs.h

// Handle to our own process.
pub const MX_HND_TYPE_PROC_SELF: u32 = 1;
// Handle to the initial thread of our own process.
pub const MX_HND_TYPE_THREAD_SELF: u32 = 2;
// Handle to the VMO containing the ELF image of the system vDSO.  This
// handle is duplicable, transferable, readable, and executable, but not
// writable.  The contents of the VM object should be treated like any
// other general-purpose ELF file image of type ET_DYN.  A process only
// needs this handle so that it can map the vDSO into new processes it
// might create or propagate it on to its children so they can do so.
// Each process's own vDSO was mapped in by its creator before the
// process started, its address passed as an argument to entry point.
pub const MX_HND_TYPE_VDSO_VMO: u32 = 3;
// Handle to the VMO used to map the initial thread's stack.  This
// handle usually has all rights.  The protocol between process creator
// and new process is that this entire VM object has been mapped in
// before the process starts.  The initial value for the SP register in
// the new process is the high edge of the mapping (assuming stacks grow
// downwards), adjusted down as required by the particular machine's C
// calling convention for function entry.  Thus the new process can
// compute its exact stack bounds by subtracting the size reported by
// this VMO from the (adjusted back up) initial SP value.
pub const MX_HND_TYPE_STACK_VMO: u32 = 4;
// Handle to a VMO containing a bootfs format image.
// The "arg" field used with this type is a simple ordinal.
pub const MX_HND_TYPE_BOOTFS_VMO: u32 = 5;
// Handle to a Job object which can be used to make child processes. The
// Job can be the same as the one used to create this process or it can
// be different.
pub const MX_HND_TYPE_JOB: u32 = 6;
// Handle types the mxio library uses
pub const MX_HND_TYPE_MXIO_ROOT: u32 = 0x10;
pub const MX_HND_TYPE_MXIO_REMOTE: u32 = 0x11;
pub const MX_HND_TYPE_MXIO_PIPE: u32 = 0x12;
pub const MX_HND_TYPE_MXIO_EVENT: u32 = 0x13;
pub const MX_HND_TYPE_MXIO_LOGGER: u32 = 0x14;
pub const MX_HND_TYPE_MXIO_CWD: u32 = 0x15;
// Handle types used by the device manager and device hosts
pub const MX_HND_TYPE_RESOURCE: u32 = 0x20;
// Handle types used by the mojo application model
pub const MX_HND_TYPE_APPLICATION_REQUEST: u32 = 0x100;
pub const MX_HND_TYPE_APPLICATION_LAUNCHER: u32 = 0x101;
pub const MX_HND_TYPE_INCOMING_SERVICES: u32 = 0x102;
pub const MX_HND_TYPE_OUTGOING_SERVICES: u32 = 0x103;
// Message pipe for dynamic loader service
pub const MX_HND_TYPE_LOADER_SVC: u32 = 0x200;
// VM object handle for the main executable file
pub const MX_HND_TYPE_EXEC_VMO: u32 = 0x201;
// Handle types for one-off use and prototyping
pub const MX_HND_TYPE_USER0: u32 = 0xFFF0;
pub const MX_HND_TYPE_USER1: u32 = 0xFFF1;
pub const MX_HND_TYPE_USER2: u32 = 0xFFF2;

#[link(name="mxio")]
extern {
    pub fn mxio_get_startup_handle(id: u32) -> mx_handle_t;
}
