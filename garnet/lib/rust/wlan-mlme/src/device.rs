// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ffi::c_void;

#[repr(C)]
pub struct Device {
    device: *mut c_void,
    deliver_ethernet: extern "C" fn(device: *mut c_void, data: *const u8, len: usize) -> i32,
}

impl Device {
    pub fn deliver_ethernet(&self, slice: &[u8]) -> i32 {
        (self.deliver_ethernet)(self.device, slice.as_ptr(), slice.len())
    }
}
