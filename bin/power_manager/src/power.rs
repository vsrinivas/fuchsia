// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate libc;

use self::libc::{int32_t, uint32_t};

use async;
use fdio::{self, fdio_sys};
use futures::{stream, TryFutureExt, TryStreamExt};
use io::{self, Result};
use std::fs::File;
use std::marker::Send;
use std::mem;
use std::os::raw;
use std::ptr;
use zx::sys::zx_handle_t;
use zx::{self, Signals};

const IOCTL_POWER_GET_INFO: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_POWER,
    1
);

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct ioctl_power_get_info_t {
    pub power_type: uint32_t,
    pub state: uint32_t,
}

const IOCTL_POWER_GET_BATTERY_INFO: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_POWER,
    2
);

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct ioctl_power_get_battery_info_t {
    pub unit: uint32_t,
    pub design_capacity: uint32_t,
    pub last_full_capacity: uint32_t,
    pub design_voltage: uint32_t,
    pub capacity_warning: uint32_t,
    pub capacity_low: uint32_t,
    pub capacity_granularity_low_warning: uint32_t,
    pub capacity_granularity_warning_full: uint32_t,
    pub present_rate: int32_t,
    pub remaining_capacity: uint32_t,
    pub present_voltage: uint32_t,
}

impl ioctl_power_get_battery_info_t {
    pub fn new() -> ioctl_power_get_battery_info_t {
        ioctl_power_get_battery_info_t::default()
    }
}

const IOCTL_POWER_GET_STATE_CHANGE_EVENT: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_GET_HANDLE,
    fdio_sys::IOCTL_FAMILY_POWER,
    3
);

pub const POWER_TYPE_AC: uint32_t = 0;
pub const POWER_TYPE_BATTERY: uint32_t = 1;

pub const POWER_STATE_ONLINE: uint32_t = 1 << 0;
pub const POWER_STATE_DISCHARGING: uint32_t = 1 << 1;
pub const POWER_STATE_CHARGING: uint32_t = 1 << 2;
pub const POWER_STATE_CRITICAL: uint32_t = 1 << 3;

pub fn get_power_info(file: &File) -> Result<ioctl_power_get_info_t> {
    let mut powerbuffer = ioctl_power_get_info_t {
        power_type: 0,
        state: 0,
    };
    let powerbuffer_ptr = &mut powerbuffer as *mut _ as *mut ::std::os::raw::c_void;

    let _ = unsafe {
        fdio::ioctl(
            &file,
            IOCTL_POWER_GET_INFO,
            ptr::null(),
            0,
            powerbuffer_ptr,
            mem::size_of::<ioctl_power_get_info_t>(),
        ).map_err(|e| e.into_io_error())?;
    };
    Ok(powerbuffer)
}

pub fn get_battery_info(file: &File) -> Result<ioctl_power_get_battery_info_t> {
    let mut batterybuffer: ioctl_power_get_battery_info_t = ioctl_power_get_battery_info_t::new();
    let batterybuffer_ptr = &mut batterybuffer as *mut _ as *mut ::std::os::raw::c_void;

    let _ = unsafe {
        fdio::ioctl(
            &file,
            IOCTL_POWER_GET_BATTERY_INFO,
            ptr::null(),
            0,
            batterybuffer_ptr,
            mem::size_of::<ioctl_power_get_battery_info_t>(),
        ).map_err(|e| e.into_io_error())?;
    };
    Ok(batterybuffer)
}

pub fn add_listener<F>(file: &File, callback: F) -> Result<()>
where
    F: 'static + Send + Fn(&File) + Sync,
{
    let mut handle: zx_handle_t = 0;
    let handle_ptr = &mut handle as *mut _ as *mut ::std::os::raw::c_void;

    unsafe {
        fdio::ioctl(
            &file,
            IOCTL_POWER_GET_STATE_CHANGE_EVENT,
            ptr::null(),
            0,
            handle_ptr,
            mem::size_of::<zx_handle_t>(),
        ).map_err(|e| e.into_io_error())?;
    };
    let h = unsafe { zx::Handle::from_raw(handle) };
    let file_copy = file
        .try_clone()
        .map_err(|e| io::Error::new(e.kind(), format!("error copying power device file: {}", e)))?;

    let f = stream::repeat(Ok(())).try_fold((callback, h, file_copy),
        |(callback, handle, file), ()| {
            // TODO: change OnSignals to wrap this so that is is not created again and again.
            async::OnSignals::new(&handle, Signals::USER_0).map_ok(|_| {
                fx_vlog!(1, "callback called {:?}", file);
                callback(&file);
                (callback, handle, file)
            })
        }
    );
    async::spawn(f.map_ok(|_| ()).unwrap_or_else(|e| {
        fx_log_err!(
            "not able to apply listener to power device, wait failed: {:?}",
            e
        )
    }));

    Ok(())
}
