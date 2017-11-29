// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate fuchsia_zircon_sys;
extern crate libc;

use fdio::{self, fdio_sys};
use io::{self, Result};
use self::fuchsia_zircon_sys::zx_handle_t;
use self::libc::{uint32_t, int32_t};
use std::fs::File;
use std::marker::Send;
use std::mem;
use std::os::raw;
use std::os::unix::io::AsRawFd;
use std::ptr;
use thread;
use zircon::{self, AsHandleRef, Signals};

const IOCTL_POWER_GET_INFO: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_POWER,
    1
);

#[repr(C)]
#[derive(Debug)]
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
#[derive(Debug)]
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

pub const BATTERY_UNIT_MW: uint32_t = 0;
pub const BATTERY_UNIT_MA: uint32_t = 1;

pub fn get_power_info(file: &File) -> Result<ioctl_power_get_info_t> {
    let mut powerbuffer: ioctl_power_get_info_t = ioctl_power_get_info_t {
        power_type: 0,
        state: 0,
    };
    let powerbuffer_ptr = &mut powerbuffer as *mut _ as *mut ::std::os::raw::c_void;

    let sz = unsafe {
        fdio::ioctl(
            file.as_raw_fd(),
            IOCTL_POWER_GET_INFO,
            ptr::null(),
            0,
            powerbuffer_ptr,
            mem::size_of::<ioctl_power_get_info_t>(),
        )
    };
    if sz < 0 {
        return Err(io::Error::from_raw_os_error(sz as i32));
    }
    Ok(powerbuffer)
}

pub fn get_battery_info(file: &File) -> Result<ioctl_power_get_battery_info_t> {
    let mut batterybuffer: ioctl_power_get_battery_info_t = ioctl_power_get_battery_info_t {
        unit: 0,
        design_capacity: 0,
        last_full_capacity: 0,
        design_voltage: 0,
        capacity_warning: 0,
        capacity_low: 0,
        capacity_granularity_low_warning: 0,
        capacity_granularity_warning_full: 0,
        present_rate: 0,
        remaining_capacity: 0,
        present_voltage: 0,
    };
    let batterybuffer_ptr = &mut batterybuffer as *mut _ as *mut ::std::os::raw::c_void;

    let sz = unsafe {
        fdio::ioctl(
            file.as_raw_fd(),
            IOCTL_POWER_GET_BATTERY_INFO,
            ptr::null(),
            0,
            batterybuffer_ptr,
            mem::size_of::<ioctl_power_get_battery_info_t>(),
        )
    };
    if sz < 0 {
        return Err(io::Error::from_raw_os_error(sz as i32));
    }
    Ok(batterybuffer)
}

pub fn add_listener<F>(file: &File, callback: F) -> Result<()>
where
    F: 'static + Send + Fn(&File),
{
    #[repr(C)]
    let mut handle: zx_handle_t = 0;
    let handle_ptr = &mut handle as *mut _ as *mut ::std::os::raw::c_void;

    let status = unsafe {
        fdio::ioctl(
            file.as_raw_fd(),
            IOCTL_POWER_GET_STATE_CHANGE_EVENT,
            ptr::null(),
            0,
            handle_ptr,
            mem::size_of::<zx_handle_t>(),
        )
    };
    if status < 0 {
        return Err(io::Error::from_raw_os_error(status as i32));
    }
    let h = unsafe { zircon::Handle::from_raw(handle) };

    let file_copy = file.try_clone().map_err(|e| {
        io::Error::new(e.kind(), format!("error copying power device file: {}", e))
    })?;

    thread::spawn(move || loop {
        if let Err(e) = h.wait_handle(Signals::USER_0, zircon::Time::INFINITE) {
            eprintln!(
                "power_manger: not able to apply listener to power device, wait failed: {:?}",
                e
            );
            break;
        } else {
            callback(&file_copy);
        }
    });
    Ok(())
}
