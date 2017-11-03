// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate ddk_rs;
extern crate ddk_sys;
extern crate fuchsia_zircon;
use fuchsia_zircon::Status;
use ddk_rs as ddk;
use ddk::{Device, DeviceOps, DriverOps};

// This is a non-bindable device that can be read from and written to.
struct SimpleDevice {
    // Returned then incremented during each read. Reset to 0 if greater than `limit`
    val: u64,
    // Set by the value given when the device is written to.
    limit: u64,
}

impl SimpleDevice {
    fn new() -> SimpleDevice {
        SimpleDevice {
            val: 0,
            limit: 10,
        }
    }
}

impl DeviceOps for SimpleDevice {
    fn name(&self) -> String {
        return String::from("simple")
    }

    fn read(&mut self, buf: &mut [u8], _offset: u64) -> Result<usize, Status> {
        if self.val >= self.limit {
            self.val = 0;
            return Ok(0);
        }
        if buf.len() < std::mem::size_of_val(&self.val) {
            return Err(Status::BUFFER_TOO_SMALL);
        }
        let u64buf: &mut u64 = unsafe { &mut *(buf.as_mut_ptr() as *mut u64) };
        *u64buf = self.val;
        self.val += 1;
        Ok(std::mem::size_of_val(&self.val))
    }

    fn write(&mut self, buf: &[u8], _offset: u64) -> Result<usize, Status> {
        let u64buf: &u64 = unsafe { &*(buf.as_ptr() as *const u64) };
        self.limit = *u64buf;
        Ok(std::mem::size_of_val(&self.limit))
    }

    fn unbind(&mut self, device: &mut Device) {
        ddk::remove_device(device);
    }
}

struct SimpleDriver {
}

impl DriverOps for SimpleDriver {
    fn bind(&mut self, parent: Device) -> Result<Device, Status> {
        let simple = Box::new(SimpleDevice::new());
        ddk::add_device(simple, Some(&parent), ddk::DEVICE_ADD_NON_BINDABLE)
    }
}

#[no_mangle]
pub extern fn init() -> Result<Box<DriverOps>, Status> {
    Ok(Box::new(SimpleDriver{}))
}
