// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Bindings for runtime services provided by Magenta

extern crate magenta;
extern crate magenta_sys;
extern crate mxruntime_sys;

use magenta::Handle;

use magenta_sys::mx_handle_t;

pub const MX_HANDLE_INVALID: mx_handle_t = 0;

// Startup handle types, derived from magenta/system/public/magenta/processargs.h

// Note: this is not a complete list, add more as use cases emerge.
#[repr(u32)]
pub enum HandleType {
    // Handle types used by the device manager and device hosts
    Resource = mxruntime_sys::MX_HND_TYPE_RESOURCE,
    // Handle types used by the mojo application model
    ApplicationRequest = mxruntime_sys::MX_HND_TYPE_APPLICATION_REQUEST,
    ApplicationLauncher = mxruntime_sys::MX_HND_TYPE_APPLICATION_LAUNCHER,
    IncomingServices = mxruntime_sys::MX_HND_TYPE_INCOMING_SERVICES,
    OutgoingServices = mxruntime_sys::MX_HND_TYPE_OUTGOING_SERVICES,
}

/// Get a startup handle of the given type, if available.
pub fn get_startup_handle(htype: HandleType) -> Option<Handle> {
    unsafe {
        let raw = mxruntime_sys::mxio_get_startup_handle(htype as u32);
        if raw == mxruntime_sys::MX_HANDLE_INVALID {
            None
        } else {
            Some(Handle::from_raw(raw))
        }
    }
}
