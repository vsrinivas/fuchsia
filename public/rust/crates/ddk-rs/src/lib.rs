// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

pub extern crate ddk_sys as sys;

extern crate byteorder;
extern crate fuchsia_zircon as zircon;

mod usb;

pub use usb::UsbProtocol;

use std::ffi::{CStr, CString};
use zircon::{ok, Handle, Signals, Status, Vmo};
use zircon::sys as zsys;
use std::slice;
use std::os::raw::c_char;

pub trait DriverOps {
    fn bind(&mut self, parent: Device) -> Result<Device, Status>;

    fn release(&mut self) {
    }
}

#[derive(Clone, Debug)]
pub struct Device {
    device: *mut sys::zx_device_t,
}

fn ctx_from_device_and_ops(device_and_ops: Box<DeviceAndOps>) -> *mut u8 {
    Box::into_raw(device_and_ops) as *mut u8
}

// `ctx` must be a unique, borrowed pointer to a `DeviceAndOps`
unsafe fn ctx_borrow_mut_device_and_ops<'a>(ctx: *mut u8) -> &'a mut DeviceAndOps {
    &mut *(ctx as *mut DeviceAndOps)
}

// `ctx` must be an owned pointer to a `DeviceAndOps`
unsafe fn ctx_into_device_and_ops(ctx: *mut u8) -> Box<DeviceAndOps> {
    Box::from_raw(ctx as *mut DeviceAndOps)
}

impl Device {
    pub unsafe fn wrap(device: *mut sys::zx_device_t) -> Device {
        Device { device: device }
    }

    pub fn add(device_ops: Box<DeviceOps>, parent: Option<&Device>, flags: AddDeviceFlags)
        -> Result<Device, Status>
    {
        let device_name = device_ops.name();
        if device_name.len() > sys::ZX_DEVICE_NAME_MAX {
            return Err(Status::INVALID_ARGS)
        }

        // Bind the CString to a local variable to ensure it lives long enough for the call below.
        let name_cstring = CString::new(device_name).map_err(|_| Status::INVALID_ARGS)?;

        let raw_parent = match parent {
            None => std::ptr::null_mut(),
            Some(device) => device.device,
        };

        // Create the Device with a null pointer initially; device_add_from_driver below will fill it
        // in.
        let device = unsafe { Device::wrap(std::ptr::null_mut()) };
        let device_and_ops = DeviceAndOps { device: device, ops: device_ops };


        let mut device_add_args = sys::device_add_args_t {
            name: name_cstring.as_ptr(),
            flags,
            ctx: ctx_from_device_and_ops(Box::new(device_and_ops)),
            // TODO: mutable static? is this actually safe?
            ops: unsafe { &mut DEVICE_OPS },
            ..Default::default()
        };


        unsafe {
            let context: &mut DeviceAndOps = ctx_borrow_mut_device_and_ops(device_add_args.ctx);
            let status = sys::device_add_from_driver(
                sys::__zircon_driver_rec__.driver,
                raw_parent,
                &mut device_add_args,
                &mut context.device.device);

            match status {
                zsys::ZX_OK => {
                    // Take a copy of the Device, which should now contain a valid zx_device_t*.
                    let device = context.device.clone();
                    Ok(device)
                },
                // Note that in this error case the context will be freed, as there shouldn't be any
                // callbacks with it.
                _ => {
                    drop(ctx_into_device_and_ops(device_add_args.ctx));
                    Err(Status::from_raw(status))
                }
            }
        }
    }

    // Should be called after unbind() has been called on the device and the driver is ready to
    // remove the device. Beware that the underlying call may free the zx_device_t.
    pub fn remove(&mut self) -> Status {
        unsafe {
            Status::from_raw(sys::device_remove(self.device))
        }
    }

    pub fn rebind(&mut self) -> Status {
        unsafe {
            Status::from_raw(sys::device_rebind(self.device))
        }
    }

    pub fn get_name(&mut self) -> String {
        unsafe {
            let name = sys::device_get_name(self.device);
            CStr::from_ptr(name).to_string_lossy().into_owned()
        }
    }

    // TODO: is this actually safe? is there always a valid parent?
    pub fn get_parent(&mut self) -> Device {
        unsafe { Self::wrap(sys::device_get_parent(self.device)) }
    }

    pub fn read(&mut self, buf: &mut [u8], offset: u64) -> Result<usize, Status> {
        let mut bytes_read = 0;
        let status = unsafe {
            sys::device_read(self.device, buf.as_mut_ptr(), buf.len(), offset, &mut bytes_read)
        };
        ok(status)?;
        Ok(bytes_read)
    }

    pub fn write(&mut self, buf: &[u8], offset: u64) -> Result<usize, Status> {
        let mut bytes_written = 0;
        let status = unsafe {
            sys::device_write(self.device, buf.as_ptr(), buf.len(), offset, &mut bytes_written)
        };
        ok(status)?;
        Ok(bytes_written)
    }

    pub fn get_size(&mut self) -> u64 {
        unsafe { sys::device_get_size(self.device) }
    }

    pub fn ioctl(&mut self, op: u32, in_buf: &[u8], out_buf: &mut [u8]) -> Result<usize, Status> {
        let mut out_actual = 0;
        let status = unsafe {
            sys::device_ioctl(self.device, op, in_buf.as_ptr(), in_buf.len(),
                out_buf.as_mut_ptr(), out_buf.len(), &mut out_actual)
        };
        ok(status)?;
        Ok(out_actual)
    }

    pub fn load_firmware(&mut self, path: &str) -> Result<(Vmo, usize), Status> {
        let mut size = 0;
        let mut fw = 0;
        let path_cstring = CString::new(path)?;
        let status = unsafe {
            sys::load_firmware(self.device, path_cstring.as_ptr(), &mut fw, &mut size)
        };
        ok(status)?;
        let vmo = Vmo::from(unsafe { Handle::from_raw(fw) });
        Ok((vmo, size))
    }

    pub fn state_clr_set(&mut self, clear_mask: Signals, set_mask: Signals) {
        unsafe {
            sys::device_state_clr_set(self.device, clear_mask.bits(), set_mask.bits())
        }
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

pub type AddDeviceFlags = sys::device_add_flags_t;

pub use sys::{
    DEVICE_ADD_NONE,
    DEVICE_ADD_NON_BINDABLE,
    DEVICE_ADD_INSTANCE,
    DEVICE_ADD_MUST_ISOLATE,
    DEVICE_ADD_INVISIBLE,
};

extern fn ddk_get_protocol(_ctx: *mut u8, _proto_id: u32, _protocol: *mut u8) -> zsys::zx_status_t {
    zsys::ZX_ERR_NOT_SUPPORTED
}

fn open_result(ret: Result<Option<Device>, Status>, dev_out: *mut *mut sys::zx_device_t) -> zsys::zx_status_t {
    match ret {
      Err(status) => status.into_raw(),
      Ok(None) => zsys::ZX_OK,
      Ok(Some(new_device)) => {
        // TODO(qwandor): This assumes that the implementor of DeviceOps.open has already called
        // add_device with the DEVICE_ADD_INSTANCE flag. Would it be better to call it here instead?
        unsafe { *dev_out = new_device.device };
        zsys::ZX_OK
      }
    }
}

unsafe extern fn ddk_open(ctx: *mut u8, dev_out: *mut *mut sys::zx_device_t, flags: u32) -> zsys::zx_status_t {
    let context = ctx_borrow_mut_device_and_ops(ctx);
    let ret = context.ops.open(flags);
    open_result(ret, dev_out)
}

unsafe extern fn ddk_open_at(ctx: *mut u8, dev_out: *mut *mut sys::zx_device_t, path: *const c_char, flags: u32) -> zsys::zx_status_t {
    let context = ctx_borrow_mut_device_and_ops(ctx);
    let path = CStr::from_ptr(path).to_str().unwrap();
    let ret = context.ops.open_at(path, flags);
    open_result(ret, dev_out)
}

unsafe extern fn ddk_close(ctx: *mut u8, flags: u32) -> zsys::zx_status_t {
    let context = ctx_borrow_mut_device_and_ops(ctx);
    let ret = context.ops.close(flags);
    ret.into_raw()
}

unsafe extern fn ddk_unbind(ctx: *mut u8) {
    let context = ctx_borrow_mut_device_and_ops(ctx);
    let unboxed = &mut *context;
    unboxed.ops.unbind(&mut unboxed.device);
}

unsafe extern fn ddk_release(ctx: *mut u8) {
    let mut context = ctx_into_device_and_ops(ctx);
    context.ops.release();
    drop(context);
}

unsafe extern fn ddk_read(ctx: *mut u8, buf: *mut u8, count: usize, off: zsys::zx_off_t, actual: *mut usize) -> zsys::zx_status_t {
    let context = ctx_borrow_mut_device_and_ops(ctx);
    let rust_buf = slice::from_raw_parts_mut(buf, count);
    match context.ops.read(rust_buf, off) {
        Ok(r) => {
            *actual = r;
            zsys::ZX_OK
        }
        Err(status) => status.into_raw()
    }
}

unsafe extern fn ddk_write(ctx: *mut u8, buf: *const u8, count: usize, off: zsys::zx_off_t, actual: *mut usize) -> zsys::zx_status_t {
    let context = ctx_borrow_mut_device_and_ops(ctx);
    let rust_buf = slice::from_raw_parts(buf, count);
    match context.ops.write(rust_buf, off) {
        Ok(r) => {
            *actual = r;
            zsys::ZX_OK
        }
        Err(status) => status.into_raw()
    }
}

unsafe extern fn ddk_get_size(ctx: *mut u8) -> u64 {
    let context = ctx_borrow_mut_device_and_ops(ctx);
    context.ops.get_size()
}

unsafe extern fn ddk_ioctl(ctx: *mut u8, op: u32, in_buf: *const u8, in_len: usize, out_buf: *mut u8, out_len: usize, out_actual: *mut usize) -> zsys::zx_status_t {
    let context = ctx_borrow_mut_device_and_ops(ctx);
    let rust_in_buf = slice::from_raw_parts(in_buf, in_len);
    let rust_out_buf = slice::from_raw_parts_mut(out_buf, out_len);
    match context.ops.ioctl(op, rust_in_buf, rust_out_buf) {
        Ok(bytes_written) => {
            *out_actual = bytes_written;
            zsys::ZX_OK
        }
        Err(status) => status.into_raw()
    }
}

unsafe extern fn ddk_suspend(ctx: *mut u8, flags: u32) -> zsys::zx_status_t {
    let context = ctx_borrow_mut_device_and_ops(ctx);
    let status = context.ops.suspend(flags);
    status.into_raw()
}

unsafe extern fn ddk_resume(ctx: *mut u8, flags: u32) -> zsys::zx_status_t {
    let context = ctx_borrow_mut_device_and_ops(ctx);
    let status = context.ops.resume(flags);
    status.into_raw()
}

static mut DEVICE_OPS: sys::zx_protocol_device_t = sys::zx_protocol_device_t {
    get_protocol: Some(ddk_get_protocol),
    open: Some(ddk_open),
    open_at: Some(ddk_open_at),
    close: Some(ddk_close),
    unbind: Some(ddk_unbind),
    release: Some(ddk_release),
    read: Some(ddk_read),
    write: Some(ddk_write),
    iotxn_queue: None,
    get_size: Some(ddk_get_size),
    ioctl: Some(ddk_ioctl),
    suspend: Some(ddk_suspend),
    resume: Some(ddk_resume),
    ..sys::DEFAULT_PROTOCOL_DEVICE
};

extern "Rust" {
    // This is the entry point for us to call the driver, implemented by the driver writer.
    #![allow(improper_ctypes)]
    fn init() -> Result<Box<DriverOps>, Status>;
}

// Requires that `ctx` is an owned pointer to a `Box<DriverOps>`.
unsafe fn ctx_into_driver_ops(ctx: *mut u8) -> Box<Box<DriverOps>> {
    Box::from_raw(ctx as *mut Box<DriverOps>)
}

// Requires that `ctx` is a unique, borrowed pointer to a `Box<DriverOps>`.
unsafe fn ctx_borrow_mut_driver_ops<'a>(ctx: *mut u8) -> &'a mut Box<DriverOps> {
    &mut *(ctx as *mut Box<DriverOps>)
}

fn ctx_from_driver_ops(ctx: Box<Box<DriverOps>>) -> *mut u8 {
    Box::into_raw(ctx) as *mut u8
}

// `out_ctx` must be a pointer to a pointer to a region in which a
// `Box<Box<DriverOps>>` can be stored.
unsafe extern fn driver_init(out_ctx: *mut *mut u8) -> zsys::zx_status_t {
    println!("driver_init called");
    match init() {
        Ok(ops) => {
            *out_ctx = ctx_from_driver_ops(Box::new(ops));
            Status::OK
        },
        Err(e) => e,
    }.into_raw()
}

// `ctx` must be a pointer to a `Box<DriverOps>`.
// `parent` must be a valid pointer to a device.
unsafe extern fn driver_bind(ctx: *mut u8, parent: *mut sys::zx_device_t) -> zsys::zx_status_t {
    println!("driver_bind called");
    let ops = ctx_borrow_mut_driver_ops(ctx);
    let parent_wrapped = Device::wrap(parent);
    let status = match ops.bind(parent_wrapped) {
        Ok(_) => Status::OK,
        Err(e) => e,
    };
    status.into_raw()
}

// `ctx` must be a pointer to a `Box<DriverOps>`
unsafe extern fn driver_release(ctx: *mut u8) {
    println!("driver_release called");
    let mut ops = ctx_into_driver_ops(ctx);
    ops.release();
    drop(ops);
}

#[no_mangle]
pub static DRIVER_OPS: sys::zx_driver_ops_t = sys::zx_driver_ops_t {
    init: Some(driver_init),
    bind: Some(driver_bind),
    release: Some(driver_release),
    ..sys::DEFAULT_DRIVER_OPS
};

// Copied from fuchsia-zircon, as we don't want to make it part of the public API.
fn into_result<T, F>(status: zsys::zx_status_t, f: F) -> Result<T, Status>
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
