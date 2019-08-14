// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{buffer::OutBuf, error::Error},
    failure::format_err,
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    std::ffi::c_void,
};

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
    /// Returns an unowned channel handle to MLME's SME peer, or ZX_HANDLE_INVALID
    /// if no SME channel is available.
    get_sme_channel: unsafe extern "C" fn(device: *mut c_void) -> u32,
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

    pub fn access_sme_sender<F: FnOnce(&fidl_mlme::MlmeEventSender) -> Result<(), fidl::Error>>(
        &self,
        fun: F,
    ) -> Result<(), Error> {
        // MLME and channel are both owned by the underlying interface.
        // A single lock protects access to both components, thus granting MLME exclusive
        // access to the channel as it's already holding the necessary lock when processing
        // service messages and frames.
        // The callback guarantees that MLME never outlives or leaks the channel.
        let handle = unsafe { (self.get_sme_channel)(self.device) };
        if handle == zx::sys::ZX_HANDLE_INVALID {
            Err(Error::Status(format!("SME channel not available"), zx::Status::BAD_HANDLE))
        } else {
            let channel = unsafe { zx::Unowned::<zx::Channel>::from_raw_handle(handle) };
            fun(&fidl_mlme::MlmeEventSender::new(&channel))
                .map_err(|e| format_err!("error sending MLME message: {}", e).into())
        }
    }
}

#[cfg(test)]
pub struct FakeDevice {
    pub eth_queue: Vec<Vec<u8>>,
    pub wlan_queue: Vec<(Vec<u8>, u32)>,
    pub sme_sap: (zx::Channel, zx::Channel),
}

#[cfg(test)]
impl FakeDevice {
    pub fn new() -> Self {
        let sme_sap = zx::Channel::create().expect("error creating channel");
        Self { eth_queue: vec![], wlan_queue: vec![], sme_sap }
    }

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

    pub extern "C" fn get_sme_channel(device: *mut c_void) -> u32 {
        use fuchsia_zircon::AsHandleRef;

        unsafe { (*(device as *mut Self)).sme_sap.0.as_handle_ref().raw_handle() }
    }

    pub fn next_mlme_msg<T: fidl::encoding::Decodable>(&mut self) -> Result<T, Error> {
        use fidl::encoding::{decode_transaction_header, Decodable, Decoder};

        let mut buf = zx::MessageBuf::new();
        let () = self
            .sme_sap
            .1
            .read(&mut buf)
            .map_err(|status| Error::Status(format!("error reading MLME message"), status))?;

        let (_, tail): (_, &[u8]) = decode_transaction_header(buf.bytes())?;
        let mut msg = Decodable::new_empty();
        Decoder::decode_into(tail, &mut [], &mut msg);
        Ok(msg)
    }

    pub fn reset(&mut self) {
        self.eth_queue.clear();
    }

    pub fn as_device(&mut self) -> Device {
        Device {
            device: self as *mut Self as *mut c_void,
            deliver_eth_frame: Self::deliver_eth_frame,
            send_wlan_frame: Self::send_wlan_frame,
            get_sme_channel: Self::get_sme_channel,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fidl::encoding::{self, decode_transaction_header, Decodable},
        wlan_common::assert_variant,
    };

    fn make_auth_confirm_msg() -> fidl_mlme::AuthenticateConfirm {
        fidl_mlme::AuthenticateConfirm {
            peer_sta_address: [1; 6],
            auth_type: fidl_mlme::AuthenticationTypes::SharedKey,
            result_code: fidl_mlme::AuthenticateResultCodes::AuthFailureTimeout,
        }
    }

    #[test]
    fn send_mlme_message() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();
        dev.access_sme_sender(|sender| sender.send_authenticate_conf(&mut make_auth_confirm_msg()))
            .expect("error sending MLME message");

        // Read message from channel.
        let mut buf = zx::MessageBuf::new();
        let msg = fake_device
            .next_mlme_msg::<fidl_mlme::AuthenticateConfirm>()
            .expect("error reading message from channel");
        assert_eq!(msg, make_auth_confirm_msg());
    }

    #[test]
    fn send_mlme_message_invalid_handle() {
        unsafe extern "C" fn get_sme_channel(device: *mut c_void) -> u32 {
            return zx::sys::ZX_HANDLE_INVALID;
        }

        let dev = Device {
            device: std::ptr::null_mut(),
            deliver_eth_frame: FakeDevice::deliver_eth_frame,
            send_wlan_frame: FakeDevice::send_wlan_frame,
            get_sme_channel,
        };

        let result = dev.access_sme_sender(|sender| {
            sender.send_authenticate_conf(&mut make_auth_confirm_msg())
        });
        assert_variant!(result, Err(Error::Status(_, zx::Status::BAD_HANDLE)));
    }

    #[test]
    fn send_mlme_message_peer_already_closed() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();

        drop(fake_device.sme_sap.1);

        let result = dev.access_sme_sender(|sender| {
            sender.send_authenticate_conf(&mut make_auth_confirm_msg())
        });
        assert_variant!(result, Err(Error::Internal(_)));
    }

    #[test]
    fn fake_device_deliver_eth_frame() {
        let mut fake_device = FakeDevice::new();
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
