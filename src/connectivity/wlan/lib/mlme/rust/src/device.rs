// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::buffer::OutBuf, fuchsia_zircon as zx, std::ffi::c_void};

pub struct TxFlags(u32);
impl TxFlags {
    pub const NONE: Self = Self(0);
    pub const PROTECTED: Self = Self(1);
    pub const FAVOR_RELIABILITY: Self = Self(1 << 1);
    // TODO(WLAN-1002): remove once MLME supports QoS tag.
    pub const QOS: Self = Self(1 << 2);
}

/// A `Device` allows transmitting frames and MLME messages.
#[repr(C)]
pub struct Device {
    device: *mut c_void,
    /// Request to deliver an Ethernet II frame to Fuchsia's Netstack.
    deliver_eth_frame: extern "C" fn(device: *mut c_void, data: *const u8, len: usize) -> i32,
    /// Request to deliver a WLAN frame over the air.
    send_wlan_frame: extern "C" fn(device: *mut c_void, buf: OutBuf, flags: u32) -> i32,
}

impl Device {
    pub fn deliver_eth_frame(&self, slice: &[u8]) -> Result<(), zx::Status> {
        let status = (self.deliver_eth_frame)(self.device, slice.as_ptr(), slice.len());
        zx::ok(status)
    }

    pub fn send_wlan_frame(&self, buf: OutBuf, flags: TxFlags) -> Result<(), zx::Status> {
        let status = (self.send_wlan_frame)(self.device, buf, flags.0);
        zx::ok(status)
    }
}

#[cfg(test)]
#[derive(Default)]
pub struct FakeDevice {
    pub eth_queue: Vec<Vec<u8>>,
    pub wlan_queue: Vec<(Vec<u8>, u32)>,
}

#[cfg(test)]
impl FakeDevice {
    pub extern "C" fn deliver_eth_frame(device: *mut c_void, data: *const u8, len: usize) -> i32 {
        assert!(!device.is_null());
        assert!(!data.is_null());
        assert_ne!(len, 0);
        // safe here because slice will not outlive data
        let slice = unsafe { std::slice::from_raw_parts(data, len) };
        // safe here because device_ptr alwyas points to self
        unsafe {
            (*(device as *mut Self)).eth_queue.push(slice.to_vec());
        }
        zx::sys::ZX_OK
    }

    pub extern "C" fn send_wlan_frame(device: *mut c_void, buf: OutBuf, flags: u32) -> i32 {
        assert!(!device.is_null());
        // safe here because device_ptr always points to Self
        unsafe {
            (*(device as *mut Self)).wlan_queue.push((buf.as_slice().to_vec(), flags));
        }
        zx::sys::ZX_OK
    }

    pub fn reset(&mut self) {
        self.eth_queue.clear();
    }

    pub fn as_device(&mut self) -> Device {
        Device {
            device: self as *mut Self as *mut c_void,
            deliver_eth_frame: Self::deliver_eth_frame,
            send_wlan_frame: Self::send_wlan_frame,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fake_device_deliver_eth_frame() {
        let mut fake_device = FakeDevice::default();
        let dev = fake_device.as_device();
        assert_eq!(fake_device.eth_queue.len(), 0);
        let first_frame = [5; 32];
        let second_frame = [6; 32];
        assert_eq!(dev.deliver_eth_frame(&first_frame[..]), Ok(()));
        assert_eq!(dev.deliver_eth_frame(&second_frame[..]), Ok(()));
        assert_eq!(fake_device.eth_queue.len(), 2);
        assert_eq!(&fake_device.eth_queue[0], &first_frame);
        assert_eq!(&fake_device.eth_queue[1], &second_frame);
    }
}
