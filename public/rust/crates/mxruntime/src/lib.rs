// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Bindings for runtime services provided by Zircon

extern crate fuchsia_zircon as zircon;
extern crate fuchsia_zircon_sys as zircon_sys;
extern crate mxruntime_sys;

use zircon::{AsHandleRef, Handle, Channel, Status};

use zircon_sys::{zx_handle_t, ZX_OK};

use mxruntime_sys::{fdio_service_connect, fdio_service_connect_at};

use std::ffi::CString;

pub const ZX_HANDLE_INVALID: zx_handle_t = 0;

// Startup handle types, derived from zircon/system/public/zircon/processargs.h

// Note: this is not a complete list, add more as use cases emerge.
#[repr(u32)]
pub enum HandleType {
    // Handle types used by the device manager and device hosts
    Resource = mxruntime_sys::PA_RESOURCE,
    // Handle types used by the mojo application model
    ServiceRequest = mxruntime_sys::PA_SERVICE_REQUEST,
    ApplicationLauncher = mxruntime_sys::PA_APP_LAUNCHER,
    OutgoingServices = mxruntime_sys::PA_APP_SERVICES,
    User0 = mxruntime_sys::PA_USER0,
}

/// Get a startup handle of the given type, if available.
pub fn get_startup_handle(htype: HandleType) -> Option<Handle> {
    unsafe {
        let raw = mxruntime_sys::zx_get_startup_handle(htype as u32);
        if raw == mxruntime_sys::ZX_HANDLE_INVALID {
            None
        } else {
            Some(Handle::from_raw(raw))
        }
    }
}

pub fn get_service_root() -> Result<Channel, Status> {
    let (h1, h2) = Channel::create()?;
    let svc = CString::new("/svc/.").unwrap();
    let connect_status = unsafe {
        fdio_service_connect(svc.as_ptr(), h1.raw_handle())
    };
    if connect_status == ZX_OK {
        Ok(h2)
    } else {
        Err(Status::from_raw(connect_status))
    }
}

pub fn connect_to_environment_service(service_root: Channel, path: &str) -> Result<Channel, Status> {
    let (h1, h2) = Channel::create()?;
    let path_str = CString::new(path).unwrap();
    let connect_status = unsafe {
        fdio_service_connect_at(service_root.raw_handle(), path_str.as_ptr(), h1.raw_handle())
    };
    if connect_status == ZX_OK {
        Ok(h2)
    } else {
        Err(Status::from_raw(connect_status))
    }
}
