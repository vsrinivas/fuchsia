// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate ddk_rs;
extern crate fuchsia_zircon_sys;
extern crate fuchsia_zircon;
use fuchsia_zircon::Status;
use fuchsia_zircon_sys as sys;
use ddk_rs as ddk;

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

impl ddk::DeviceOps for SimpleDevice {
    fn name(&self) -> String {
        return String::from("simple")
    }

    fn read(&mut self, buf: &mut [u8], _offset: u64) -> Result<usize, Status> {
        if self.val >= self.limit {
            self.val = 0;
            return Ok(0);
        }
        if buf.len() < std::mem::size_of_val(&self.val) {
            return Err(Status::ErrBufferTooSmall);
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
}

// TODO: move this somewhere else to isolate usage of ddk_sys
//       OR figure out how to provide hooks so that ddk_sys calls like this can be wrapped
#[no_mangle]
pub extern fn simple_init(mut _out_ctx: *mut *mut u8) -> sys::zx_status_t {
    let simple = Box::new(SimpleDevice::new());
    match ddk::add_device(simple, ddk::DEVICE_ADD_NON_BINDABLE) {
        Ok(device) => { _out_ctx = Box::into_raw(Box::new(Box::new(device))) as *mut *mut u8; sys::ZX_OK }
        Err(error) => error as sys::zx_status_t
    }
}
