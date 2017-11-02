// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate byteorder;
extern crate ddk_sys;
extern crate fuchsia_zircon as zircon;

mod usb;

pub use usb::UsbProtocol;

use std::ffi::{CStr, CString};
use zircon::Status;
use zircon::sys as sys;
use std::slice;
use std::os::raw::c_char;

pub struct Driver {
    driver: *mut ddk_sys::zx_driver_t,
}

impl Driver {
    pub fn wrap(driver: *mut ddk_sys::zx_driver_t) -> Driver {
        Driver { driver: driver }
    }
}

#[derive(Clone, Debug)]
pub struct Device {
    device: *mut ddk_sys::zx_device_t,
}

impl Device {
    pub fn wrap(device: *mut ddk_sys::zx_device_t) -> Device {
        Device { device: device }
    }
}

pub trait DeviceOps {
    fn name(&self) -> String;

    fn open(&mut self, _flags: u32) -> Result<Option<Device>, Status> {
        Ok(None)
    }

    fn open_at(&mut self, _path: &str, _flags: u32) -> Result<Option<Device>, Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn close(&mut self, _flags: u32) -> Status {
        Status::OK
    }

    fn unbind(&mut self, _device: &mut Device) {
    }

    fn release(&mut self) {
    }

    fn read(&mut self, _buf: &mut [u8], _offset: u64) -> Result<usize, Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn write(&mut self, _buf: &[u8], _offset: u64) -> Result<usize, Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn get_size(&mut self) -> u64 {
        0
    }

    fn ioctl(&mut self, _op: u32, _in_buf: &[u8], _out_buf: &mut [u8]) -> Result<usize, Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn suspend(&mut self, _flags: u32) -> Status {
        Status::NOT_SUPPORTED
    }

    fn resume(&mut self, _flags: u32) -> Status {
        Status::NOT_SUPPORTED
    }
}

struct DeviceAndOps {
    device: Device,
    ops: Box<DeviceOps>,
}

pub type AddDeviceFlags = ddk_sys::device_add_flags_t;

pub use ddk_sys::{
    DEVICE_ADD_NONE,
    DEVICE_ADD_NON_BINDABLE,
    DEVICE_ADD_INSTANCE,
    DEVICE_ADD_MUST_ISOLATE,
    DEVICE_ADD_INVISIBLE,
};

pub fn add_device(device_ops: Box<DeviceOps>, parent: Option<&Device>, flags: AddDeviceFlags)
    -> Result<Device, Status>
{
    let device_name = device_ops.name();
    if device_name.len() > ddk_sys::ZX_DEVICE_NAME_MAX {
        return Err(Status::INVALID_ARGS)
    }

    let raw_parent = match parent {
        None => std::ptr::null_mut(),
        Some(device) => device.device,
    };

    let mut device_add_args: ddk_sys::device_add_args_t = ddk_sys::device_add_args_t::new();

    // Create the Device with a null pointer initially; device_add_from_driver below will fill it
    // in.
    let device = Device::wrap(std::ptr::null_mut());
    let device_and_ops = DeviceAndOps { device: device, ops: device_ops };
    device_add_args.ctx = Box::into_raw(Box::new(device_and_ops)) as *mut u8;
    let mut context: Box<DeviceAndOps> = unsafe {
        Box::from_raw(device_add_args.ctx as *mut DeviceAndOps)
    };

    // Bind the CString to a local variable to ensure it lives long enough for the call below.
    let name_cstring = CString::new(device_name).unwrap();
    device_add_args.name = name_cstring.as_ptr();
    device_add_args.flags = flags;

    unsafe {
        device_add_args.ops = &mut DEVICE_OPS;
        let status = ddk_sys::device_add_from_driver(ddk_sys::__zircon_driver_rec__.driver, raw_parent,
            &mut device_add_args, &mut context.device.device);
        match status {
            sys::ZX_OK => {
                // Take a copy of the Device, which should now contain a valid zx_device_t*.
                let device = context.device.clone();
                // Make sure the context doesn't get freed by Rust yet.
                let _ = Box::into_raw(context);
                Ok(device)
            },
            // Note that in this error case the context will be freed, as there shouldn't be any
            // callbacks with it.
            _ => Err(Status::from_raw(status)),
        }
    }
}

// Should be called after unbind() has been called on the device and the driver is ready to remove
// the device. Beware that the underlying call may free the zx_device_t.
pub fn remove_device(device: &mut Device) -> Status {
    unsafe {
        Status::from_raw(ddk_sys::device_remove(device.device))
    }
}

extern fn ddk_get_protocol(ctx: *mut u8, proto_id: u32, protocol: *mut u8) -> sys::zx_status_t {
    sys::ZX_ERR_NOT_SUPPORTED
}

fn open_result(ret: Result<Option<Device>, Status>, dev_out: *mut *mut ddk_sys::zx_device_t) -> sys::zx_status_t {
    match ret {
      Err(status) => status.into_raw(),
      Ok(None) => sys::ZX_OK,
      Ok(Some(new_device)) => {
        // TODO(qwandor): This assumes that the implementor of DeviceOps.open has already called
        // add_device with the DEVICE_ADD_INSTANCE flag. Would it be better to call it here instead?
        unsafe { *dev_out = new_device.device };
        sys::ZX_OK
      }
    }
}

extern fn ddk_open(ctx: *mut u8, dev_out: *mut *mut ddk_sys::zx_device_t, flags: u32) -> sys::zx_status_t {
    let mut context: Box<DeviceAndOps> = unsafe { Box::from_raw(ctx as *mut DeviceAndOps) };
    let ret = context.ops.open(flags);
    let _ = Box::into_raw(context);
    open_result(ret, dev_out)
}

extern fn ddk_open_at(ctx: *mut u8, dev_out: *mut *mut ddk_sys::zx_device_t, path: *const c_char, flags: u32) -> sys::zx_status_t {
    let mut context: Box<DeviceAndOps> = unsafe { Box::from_raw(ctx as *mut DeviceAndOps) };
    let path = unsafe { CStr::from_ptr(path).to_str().unwrap() };
    let ret = context.ops.open_at(path, flags);
    let _ = Box::into_raw(context);
    open_result(ret, dev_out)
}

extern fn ddk_close(ctx: *mut u8, flags: u32) -> sys::zx_status_t {
    let mut context: Box<DeviceAndOps> = unsafe { Box::from_raw(ctx as *mut DeviceAndOps) };
    let ret = context.ops.close(flags);
    let _ = Box::into_raw(context);
    ret.into_raw()
}

extern fn ddk_unbind(ctx: *mut u8) {
    let mut context: Box<DeviceAndOps> = unsafe { Box::from_raw(ctx as *mut DeviceAndOps) };
    {
        let unboxed = &mut *context;
        unboxed.ops.unbind(&mut unboxed.device);
    }
    let _ = Box::into_raw(context);
}

extern fn ddk_release(ctx: *mut u8) {
    let mut context: Box<DeviceAndOps> = unsafe { Box::from_raw(ctx as *mut DeviceAndOps) };
    context.ops.release();
    // Don't Box::into_raw the device so it can be freed by Rust
}

extern fn ddk_read(ctx: *mut u8, buf: *mut u8, count: usize, off: sys::zx_off_t, actual: *mut usize) -> sys::zx_status_t {
    let mut context: Box<DeviceAndOps> = unsafe { Box::from_raw(ctx as *mut DeviceAndOps) };
    let rust_buf = unsafe { slice::from_raw_parts_mut(buf, count) };
    let ret = match context.ops.read(rust_buf, off) {
        Ok(r) => {
                unsafe { *actual = r };
                sys::ZX_OK
            },
        // TODO(qwandor): Investigate adding a to_raw method for Status.
        Err(status) => status.into_raw()
    };
    let _ = Box::into_raw(context);
    ret
}

extern fn ddk_write(ctx: *mut u8, buf: *const u8, count: usize, off: sys::zx_off_t, actual: *mut usize) -> sys::zx_status_t {
    let mut context: Box<DeviceAndOps> = unsafe { Box::from_raw(ctx as *mut DeviceAndOps) };
    let rust_buf = unsafe { slice::from_raw_parts(buf, count) };
    let ret = match context.ops.write(rust_buf, off) {
        Ok(r) => {
                unsafe { *actual = r };
                sys::ZX_OK
            },
        Err(status) => status.into_raw()
    };
    let _ = Box::into_raw(context);
    ret
}

extern fn ddk_iotxn_queue(ctx: *mut u8, txn: *mut ddk_sys::iotxn_t) {
}

extern fn ddk_get_size(ctx: *mut u8) -> u64 {
    let mut context: Box<DeviceAndOps> = unsafe { Box::from_raw(ctx as *mut DeviceAndOps) };
    let size = context.ops.get_size();
    let _ = Box::into_raw(context);
    size
}

extern fn ddk_ioctl(ctx: *mut u8, op: u32, in_buf: *const u8, in_len: usize, out_buf: *mut u8, out_len: usize, out_actual: *mut usize) -> sys::zx_status_t {
    let mut context: Box<DeviceAndOps> = unsafe { Box::from_raw(ctx as *mut DeviceAndOps) };
    let rust_in_buf = unsafe { slice::from_raw_parts(in_buf, in_len) };
    let rust_out_buf = unsafe { slice::from_raw_parts_mut(out_buf, out_len) };
    let status = match context.ops.ioctl(op, rust_in_buf, rust_out_buf) {
        Ok(bytes_written) => {
            unsafe { *out_actual = bytes_written };
            sys::ZX_OK
        }
        Err(status) => status.into_raw()
    };
    let _ = Box::into_raw(context);
    status
}

extern fn ddk_suspend(ctx: *mut u8, flags: u32) -> sys::zx_status_t {
    let mut context: Box<DeviceAndOps> = unsafe { Box::from_raw(ctx as *mut DeviceAndOps) };
    let status = context.ops.suspend(flags);
    let _ = Box::into_raw(context);
    status.into_raw()
}

extern fn ddk_resume(ctx: *mut u8, flags: u32) -> sys::zx_status_t {
    let mut context: Box<DeviceAndOps> = unsafe { Box::from_raw(ctx as *mut DeviceAndOps) };
    let status = context.ops.resume(flags);
    let _ = Box::into_raw(context);
    status.into_raw()
}

static mut DEVICE_OPS: ddk_sys::zx_protocol_device_t = ddk_sys::zx_protocol_device_t {
    get_protocol: Some(ddk_get_protocol),
    open: Some(ddk_open),
    open_at: Some(ddk_open_at),
    close: Some(ddk_close),
    unbind: Some(ddk_unbind),
    release: Some(ddk_release),
    read: Some(ddk_read),
    write: Some(ddk_write),
    iotxn_queue: Some(ddk_iotxn_queue),
    get_size: Some(ddk_get_size),
    ioctl: Some(ddk_ioctl),
    suspend: Some(ddk_suspend),
    resume: Some(ddk_resume),
    ..ddk_sys::DEFAULT_PROTOCOL_DEVICE
};

// Copied from fuchsia-zircon, as we don't want to make it part of the public API.
fn into_result<T, F>(status: sys::zx_status_t, f: F) -> Result<T, Status>
    where F: FnOnce() -> T {
    // All non-negative values are assumed successful. Note: calls that don't try
    // to multiplex success values into status return could be more strict here.
    if status >= 0 {
        Ok(f())
    } else {
        Err(Status::from_raw(status))
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
    }
}
