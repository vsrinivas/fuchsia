// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{buffer::OutBuf, error::Error, key},
    banjo_ddk_protocol_wlan_info::*,
    failure::format_err,
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    std::ffi::c_void,
    wlan_common::TimeUnit,
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
    /// Returns the currently set WLAN channel.
    get_wlan_channel: extern "C" fn(device: *mut c_void) -> WlanChannel,
    /// Request the PHY to change its channel. If successful, get_wlan_channel will return the
    /// chosen channel.
    set_wlan_channel: extern "C" fn(device: *mut c_void, channel: WlanChannel) -> i32,
    /// Set a key on the device.
    /// |key| is mutable because the underlying API does not take a const wlan_key_config_t.
    set_key: extern "C" fn(device: *mut c_void, key: *mut key::KeyConfig) -> i32,
    /// Configure the device's BSS.
    /// |cfg| is mutable because the underlying API does not take a const wlan_bss_config_t.
    configure_bss: extern "C" fn(device: *mut c_void, cfg: *mut WlanBssConfig) -> i32,
    /// Enable hardware offload of beaconing on the device.
    enable_beaconing: extern "C" fn(
        device: *mut c_void,
        beacon_tmpl_data: *const u8,
        beacon_tmpl_len: usize,
        tim_ele_offset: usize,
        beacon_interval: u16,
    ) -> i32,
    /// Disable beaconing on the device.
    disable_beaconing: extern "C" fn(device: *mut c_void) -> i32,
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

    #[allow(deprecated)] // Until Rust MLME is powered by a Rust-written message loop.
    pub fn access_sme_sender<F: FnOnce(&fidl_mlme::MlmeServerSender) -> Result<(), fidl::Error>>(
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
            fun(&fidl_mlme::MlmeServerSender::new(&channel))
                .map_err(|e| format_err!("error sending MLME message: {}", e).into())
        }
    }

    pub fn set_channel(&self, chan: WlanChannel) -> Result<(), zx::Status> {
        let status = (self.set_wlan_channel)(self.device, chan);
        zx::ok(status)
    }

    pub fn set_key(&self, mut key: key::KeyConfig) -> Result<(), zx::Status> {
        let status = (self.set_key)(self.device, &mut key as *mut key::KeyConfig);
        zx::ok(status)
    }

    pub fn channel(&self) -> WlanChannel {
        (self.get_wlan_channel)(self.device)
    }

    pub fn configure_bss(&self, mut cfg: WlanBssConfig) -> Result<(), zx::Status> {
        let status = (self.configure_bss)(self.device, &mut cfg as *mut WlanBssConfig);
        zx::ok(status)
    }

    pub fn enable_beaconing(
        &self,
        beacon_tmpl: &[u8],
        tim_ele_offset: usize,
        beacon_interval: TimeUnit,
    ) -> Result<(), zx::Status> {
        let status = (self.enable_beaconing)(
            self.device,
            beacon_tmpl.as_ptr(),
            beacon_tmpl.len(),
            tim_ele_offset,
            beacon_interval.into(),
        );
        zx::ok(status)
    }

    pub fn disable_beaconing(&self) -> Result<(), zx::Status> {
        let status = (self.disable_beaconing)(self.device);
        zx::ok(status)
    }
}

#[cfg(test)]
pub struct FakeDevice {
    pub eth_queue: Vec<Vec<u8>>,
    pub wlan_queue: Vec<(Vec<u8>, u32)>,
    pub sme_sap: (zx::Channel, zx::Channel),
    pub wlan_channel: WlanChannel,
    pub keys: Vec<key::KeyConfig>,
    pub bss_cfg: Option<WlanBssConfig>,
    pub bcn_cfg: Option<(Vec<u8>, usize, TimeUnit)>,
}

#[cfg(test)]
impl FakeDevice {
    pub fn new() -> Self {
        let sme_sap = zx::Channel::create().expect("error creating channel");
        Self {
            eth_queue: vec![],
            wlan_queue: vec![],
            sme_sap,
            wlan_channel: WlanChannel {
                primary: 0,
                cbw: WlanChannelBandwidth::_20,
                secondary80: 0,
            },
            keys: vec![],
            bss_cfg: None,
            bcn_cfg: None,
        }
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

    pub extern "C" fn send_wlan_frame_with_failure(_: *mut c_void, _: OutBuf, _: u32) -> i32 {
        zx::sys::ZX_ERR_IO
    }

    pub extern "C" fn get_sme_channel(device: *mut c_void) -> u32 {
        use fuchsia_zircon::AsHandleRef;

        unsafe { (*(device as *mut Self)).sme_sap.0.as_handle_ref().raw_handle() }
    }

    pub extern "C" fn get_wlan_channel(device: *mut c_void) -> WlanChannel {
        unsafe { (*(device as *const Self)).wlan_channel }
    }

    pub extern "C" fn set_wlan_channel(device: *mut c_void, wlan_channel: WlanChannel) -> i32 {
        unsafe {
            (*(device as *mut Self)).wlan_channel = wlan_channel;
        }
        zx::sys::ZX_OK
    }

    pub extern "C" fn set_key(device: *mut c_void, key: *mut key::KeyConfig) -> i32 {
        unsafe {
            (*(device as *mut Self)).keys.push((*key).clone());
        }
        zx::sys::ZX_OK
    }

    pub extern "C" fn configure_bss(device: *mut c_void, cfg: *mut WlanBssConfig) -> i32 {
        unsafe {
            (*(device as *mut Self)).bss_cfg.replace((*cfg).clone());
        }
        zx::sys::ZX_OK
    }

    pub extern "C" fn enable_beaconing(
        device: *mut c_void,
        beacon_tmpl_data: *const u8,
        beacon_tmpl_len: usize,
        tim_ele_offset: usize,
        beacon_interval: u16,
    ) -> i32 {
        unsafe {
            (*(device as *mut Self)).bcn_cfg = Some((
                std::slice::from_raw_parts(beacon_tmpl_data, beacon_tmpl_len).to_vec(),
                tim_ele_offset,
                beacon_interval.into(),
            ));
        }
        zx::sys::ZX_OK
    }

    pub extern "C" fn disable_beaconing(device: *mut c_void) -> i32 {
        unsafe {
            (*(device as *mut Self)).bcn_cfg = None;
        }
        zx::sys::ZX_OK
    }

    pub fn next_mlme_msg<T: fidl::encoding::Decodable>(&mut self) -> Result<T, Error> {
        use fidl::encoding::{decode_transaction_header, Decodable, Decoder};

        let mut buf = zx::MessageBuf::new();
        let () = self
            .sme_sap
            .1
            .read(&mut buf)
            .map_err(|status| Error::Status(format!("error reading MLME message"), status))?;

        let (header, tail): (_, &[u8]) = decode_transaction_header(buf.bytes())?;
        let mut msg = Decodable::new_empty();
        Decoder::decode_into(&header, tail, &mut [], &mut msg)
            .expect("error decoding MLME message");
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
            get_wlan_channel: Self::get_wlan_channel,
            set_wlan_channel: Self::set_wlan_channel,
            set_key: Self::set_key,
            configure_bss: Self::configure_bss,
            enable_beaconing: Self::enable_beaconing,
            disable_beaconing: Self::disable_beaconing,
        }
    }

    pub fn as_device_fail_wlan_tx(&mut self) -> Device {
        Device {
            device: self as *mut Self as *mut c_void,
            deliver_eth_frame: Self::deliver_eth_frame,
            send_wlan_frame: Self::send_wlan_frame_with_failure,
            get_sme_channel: Self::get_sme_channel,
            set_wlan_channel: Self::set_wlan_channel,
            get_wlan_channel: Self::get_wlan_channel,
            set_key: Self::set_key,
            configure_bss: Self::configure_bss,
            enable_beaconing: Self::enable_beaconing,
            disable_beaconing: Self::disable_beaconing,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use wlan_common::assert_variant;

    fn make_auth_confirm_msg() -> fidl_mlme::AuthenticateConfirm {
        fidl_mlme::AuthenticateConfirm {
            peer_sta_address: [1; 6],
            auth_type: fidl_mlme::AuthenticationTypes::SharedKey,
            result_code: fidl_mlme::AuthenticateResultCodes::AuthFailureTimeout,
            auth_content: None,
        }
    }

    #[test]
    fn send_mlme_message() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();
        dev.access_sme_sender(|sender| sender.send_authenticate_conf(&mut make_auth_confirm_msg()))
            .expect("error sending MLME message");

        // Read message from channel.
        let msg = fake_device
            .next_mlme_msg::<fidl_mlme::AuthenticateConfirm>()
            .expect("error reading message from channel");
        assert_eq!(msg, make_auth_confirm_msg());
    }

    #[test]
    fn send_mlme_message_invalid_handle() {
        unsafe extern "C" fn get_sme_channel(_device: *mut c_void) -> u32 {
            return zx::sys::ZX_HANDLE_INVALID;
        }

        let dev = Device {
            device: std::ptr::null_mut(),
            deliver_eth_frame: FakeDevice::deliver_eth_frame,
            send_wlan_frame: FakeDevice::send_wlan_frame,
            get_sme_channel,
            get_wlan_channel: FakeDevice::get_wlan_channel,
            set_wlan_channel: FakeDevice::set_wlan_channel,
            set_key: FakeDevice::set_key,
            configure_bss: FakeDevice::configure_bss,
            enable_beaconing: FakeDevice::enable_beaconing,
            disable_beaconing: FakeDevice::disable_beaconing,
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

    #[test]
    fn get_set_channel() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();
        dev.set_channel(WlanChannel {
            primary: 2,
            cbw: WlanChannelBandwidth::_80P80,
            secondary80: 4,
        })
        .expect("set_channel failed?");
        // Check the internal state.
        assert_eq!(
            fake_device.wlan_channel,
            WlanChannel { primary: 2, cbw: WlanChannelBandwidth::_80P80, secondary80: 4 }
        );
        // Check the external view of the internal state.
        assert_eq!(
            dev.channel(),
            WlanChannel { primary: 2, cbw: WlanChannelBandwidth::_80P80, secondary80: 4 }
        );
    }

    #[test]
    fn set_key() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();
        dev.set_key(key::KeyConfig {
            bssid: 1,
            protection: key::Protection::NONE,
            cipher_oui: [3, 4, 5],
            cipher_type: 6,
            key_type: key::KeyType::PAIRWISE,
            peer_addr: [8; 6],
            key_idx: 9,
            key_len: 10,
            key: [11; 32],
            rsc: 12,
        })
        .expect("error setting key");
        assert_eq!(fake_device.keys.len(), 1);
    }

    #[test]
    fn configure_bss() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();
        dev.configure_bss(WlanBssConfig {
            bssid: [1, 2, 3, 4, 5, 6],
            bss_type: WlanBssType::PERSONAL,
            remote: true,
        })
        .expect("error setting key");
        assert!(fake_device.bss_cfg.is_some());
    }

    #[test]
    fn enable_disable_beaconing() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();
        dev.enable_beaconing(&[1, 2, 3, 4][..], 1, 2.into()).expect("error enabling beaconing");
        assert!(fake_device.bcn_cfg.is_some());
        dev.disable_beaconing().expect("error disabling beaconing");
        assert!(fake_device.bcn_cfg.is_none());
    }
}
