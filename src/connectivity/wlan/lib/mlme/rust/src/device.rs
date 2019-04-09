// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_zircon as zx, std::ffi::c_void};

#[repr(C)]
pub struct Device {
    device: *mut c_void,
    deliver_ethernet: extern "C" fn(device: *mut c_void, data: *const u8, len: usize) -> i32,
}

impl Device {
    pub fn deliver_ethernet(&self, slice: &[u8]) -> Result<(), zx::Status> {
        let status = (self.deliver_ethernet)(self.device, slice.as_ptr(), slice.len());
        zx::ok(status)
    }
}

#[cfg(test)]
#[derive(Default)]
pub struct FakeDevice {
    pub eth_queue: Vec<Vec<u8>>,
}

#[cfg(test)]
impl FakeDevice {
    pub extern "C" fn deliver_ethernet(device: *mut c_void, data: *const u8, len: usize) -> i32 {
        assert!(!device.is_null());
        assert!(!data.is_null());
        assert!(len != 0);
        // safe here because slice will not outlive data
        let slice = unsafe { std::slice::from_raw_parts(data, len) };
        // safe here because device_ptr alwyas points to self
        unsafe {
            (*(device as *mut Self)).eth_queue.push(slice.to_vec());
        }
        zx::sys::ZX_OK
    }

    pub fn reset(&mut self) {
        self.eth_queue.clear();
    }

    pub fn as_device(&mut self) -> Device {
        Device {
            device: self as *mut Self as *mut c_void,
            deliver_ethernet: Self::deliver_ethernet,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fake_device_deliver_ethernet() {
        let mut fake_device = FakeDevice::default();
        assert_eq!(fake_device.eth_queue.len(), 0);
        let first_frame = [5; 32];
        let second_frame = [6; 32];
        assert_eq!(fake_device.as_device().deliver_ethernet(&first_frame[..]), Ok(()));
        assert_eq!(fake_device.as_device().deliver_ethernet(&second_frame[..]), Ok(()));
        assert_eq!(fake_device.eth_queue.len(), 2);
        assert_eq!(&fake_device.eth_queue[0], &first_frame);
        assert_eq!(&fake_device.eth_queue[1], &second_frame);
    }
}
