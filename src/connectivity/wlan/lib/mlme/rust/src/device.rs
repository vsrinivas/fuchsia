// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{buffer::OutBuf, error::Error, key},
    anyhow::format_err,
    banjo_ddk_protocol_wlan_info::*,
    banjo_ddk_protocol_wlan_mac::{WlanHwScanConfig, WlanmacInfo},
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    std::ffi::c_void,
    wlan_common::{mac::MacAddr, TimeUnit},
};

#[cfg(test)]
pub use test_utils::*;

#[derive(Debug, PartialEq)]
pub struct LinkStatus(u8);
impl LinkStatus {
    pub const DOWN: Self = Self(0);
    pub const UP: Self = Self(1);
}

impl From<fidl_mlme::ControlledPortState> for LinkStatus {
    fn from(state: fidl_mlme::ControlledPortState) -> Self {
        match state {
            fidl_mlme::ControlledPortState::Open => Self::UP,
            fidl_mlme::ControlledPortState::Closed => Self::DOWN,
        }
    }
}

#[derive(Debug)]
pub struct TxFlags(pub u32);
impl TxFlags {
    pub const NONE: Self = Self(0);
    pub const PROTECTED: Self = Self(1);
    pub const FAVOR_RELIABILITY: Self = Self(1 << 1);
    // TODO(fxbug.dev/29622): remove once MLME supports QoS tag.
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
    /// Make scan request to the driver
    start_hw_scan: extern "C" fn(device: *mut c_void, config: *const WlanHwScanConfig) -> i32,
    /// Get information and capabilities of this WLAN interface
    get_wlan_info: extern "C" fn(device: *mut c_void) -> WlanmacInfo,
    /// Configure the device's BSS.
    /// |cfg| is mutable because the underlying API does not take a const wlan_bss_config_t.
    configure_bss: extern "C" fn(device: *mut c_void, cfg: *mut WlanBssConfig) -> i32,
    /// Enable hardware offload of beaconing on the device.
    enable_beaconing: extern "C" fn(
        device: *mut c_void,
        buf: OutBuf,
        tim_ele_offset: usize,
        beacon_interval: u16,
    ) -> i32,
    /// Disable beaconing on the device.
    disable_beaconing: extern "C" fn(device: *mut c_void) -> i32,
    /// Reconfigure the enabled beacon on the device.
    configure_beacon: extern "C" fn(device: *mut c_void, buf: OutBuf) -> i32,
    /// Sets the link status to be UP or DOWN.
    set_link_status: extern "C" fn(device: *mut c_void, status: u8) -> i32,
    /// Configure the association context.
    /// |assoc_ctx| is mutable because the underlying API does not take a const wlan_assoc_ctx_t.
    configure_assoc: extern "C" fn(device: *mut c_void, assoc_ctx: *mut WlanAssocCtx) -> i32,
    /// Clear the association context.
    clear_assoc: extern "C" fn(device: *mut c_void, addr: &[u8; 6]) -> i32,
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
    pub fn access_sme_sender<
        F: FnOnce(&fidl_mlme::MlmeServerSender<'_>) -> Result<(), fidl::Error>,
    >(
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

    pub fn start_hw_scan(&self, config: &WlanHwScanConfig) -> Result<(), zx::Status> {
        let status = (self.start_hw_scan)(self.device, config as *const WlanHwScanConfig);
        zx::ok(status)
    }

    pub fn channel(&self) -> WlanChannel {
        (self.get_wlan_channel)(self.device)
    }

    pub fn wlan_info(&self) -> WlanmacInfo {
        (self.get_wlan_info)(self.device)
    }

    pub fn configure_bss(&self, mut cfg: WlanBssConfig) -> Result<(), zx::Status> {
        let status = (self.configure_bss)(self.device, &mut cfg as *mut WlanBssConfig);
        zx::ok(status)
    }

    pub fn enable_beaconing(
        &self,
        buf: OutBuf,
        tim_ele_offset: usize,
        beacon_interval: TimeUnit,
    ) -> Result<(), zx::Status> {
        let status =
            (self.enable_beaconing)(self.device, buf, tim_ele_offset, beacon_interval.into());
        zx::ok(status)
    }

    pub fn disable_beaconing(&self) -> Result<(), zx::Status> {
        let status = (self.disable_beaconing)(self.device);
        zx::ok(status)
    }

    pub fn configure_beacon(&self, buf: OutBuf) -> Result<(), zx::Status> {
        let status = (self.configure_beacon)(self.device, buf);
        zx::ok(status)
    }

    pub fn set_eth_link(&self, status: LinkStatus) -> Result<(), zx::Status> {
        let status = (self.set_link_status)(self.device, status.0);
        zx::ok(status)
    }

    pub fn set_eth_link_up(&self) -> Result<(), zx::Status> {
        let status = (self.set_link_status)(self.device, LinkStatus::UP.0);
        zx::ok(status)
    }

    pub fn set_eth_link_down(&self) -> Result<(), zx::Status> {
        let status = (self.set_link_status)(self.device, LinkStatus::DOWN.0);
        zx::ok(status)
    }

    pub fn configure_assoc(&self, mut assoc_ctx: WlanAssocCtx) -> Result<(), zx::Status> {
        let status = (self.configure_assoc)(self.device, &mut assoc_ctx as *mut WlanAssocCtx);
        zx::ok(status)
    }

    pub fn clear_assoc(&self, addr: &MacAddr) -> Result<(), zx::Status> {
        let status = (self.clear_assoc)(self.device, addr);
        zx::ok(status)
    }
}

#[cfg(test)]
macro_rules! arr {
    ($slice:expr, $size:expr) => {{
        assert!($slice.len() < $size);
        let mut a = [0; $size];
        a[..$slice.len()].clone_from_slice(&$slice);
        a
    }};
}

#[cfg(test)]
mod test_utils {
    use {
        super::*,
        crate::buffer::{BufferProvider, FakeBufferProvider},
        banjo_ddk_hw_wlan_ieee80211::*,
        banjo_ddk_hw_wlan_wlaninfo::*,
    };

    pub struct FakeDevice {
        pub eth_queue: Vec<Vec<u8>>,
        pub wlan_queue: Vec<(Vec<u8>, u32)>,
        pub sme_sap: (zx::Channel, zx::Channel),
        pub wlan_channel: WlanChannel,
        pub keys: Vec<key::KeyConfig>,
        pub hw_scan_req: Option<WlanHwScanConfig>,
        pub info: WlanmacInfo,
        pub bss_cfg: Option<WlanBssConfig>,
        pub bcn_cfg: Option<(Vec<u8>, usize, TimeUnit)>,
        pub link_status: LinkStatus,
        pub assocs: std::collections::HashMap<MacAddr, WlanAssocCtx>,
        pub buffer_provider: BufferProvider,
    }

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
                hw_scan_req: None,
                info: fake_wlanmac_info(),
                keys: vec![],
                bss_cfg: None,
                bcn_cfg: None,
                link_status: LinkStatus::DOWN,
                assocs: std::collections::HashMap::new(),
                buffer_provider: FakeBufferProvider::new(),
            }
        }

        pub extern "C" fn deliver_eth_frame(
            device: *mut c_void,
            data: *const u8,
            len: usize,
        ) -> i32 {
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
            buf.free();
            zx::sys::ZX_OK
        }

        pub extern "C" fn set_link_status(device: *mut c_void, status: u8) -> i32 {
            assert!(!device.is_null());
            // safe here because device_ptr always points to Self
            unsafe {
                (*(device as *mut Self)).link_status = LinkStatus(status);
            }
            zx::sys::ZX_OK
        }

        pub extern "C" fn send_wlan_frame_with_failure(_: *mut c_void, buf: OutBuf, _: u32) -> i32 {
            buf.free();
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

        pub extern "C" fn start_hw_scan(
            device: *mut c_void,
            config: *const WlanHwScanConfig,
        ) -> i32 {
            unsafe {
                (*(device as *mut Self)).hw_scan_req = Some((*config).clone());
            }
            zx::sys::ZX_OK
        }

        pub extern "C" fn start_hw_scan_fails(
            _device: *mut c_void,
            _config: *const WlanHwScanConfig,
        ) -> i32 {
            zx::sys::ZX_ERR_NOT_SUPPORTED
        }

        pub extern "C" fn get_wlan_info(device: *mut c_void) -> WlanmacInfo {
            unsafe { (*(device as *const Self)).info }
        }

        pub extern "C" fn configure_bss(device: *mut c_void, cfg: *mut WlanBssConfig) -> i32 {
            unsafe {
                (*(device as *mut Self)).bss_cfg.replace((*cfg).clone());
            }
            zx::sys::ZX_OK
        }

        pub extern "C" fn enable_beaconing(
            device: *mut c_void,
            buf: OutBuf,
            tim_ele_offset: usize,
            beacon_interval: u16,
        ) -> i32 {
            unsafe {
                (*(device as *mut Self)).bcn_cfg =
                    Some((buf.as_slice().to_vec(), tim_ele_offset, TimeUnit(beacon_interval)));
                buf.free();
            }
            zx::sys::ZX_OK
        }

        pub extern "C" fn disable_beaconing(device: *mut c_void) -> i32 {
            unsafe {
                (*(device as *mut Self)).bcn_cfg = None;
            }
            zx::sys::ZX_OK
        }

        pub extern "C" fn configure_beacon(device: *mut c_void, buf: OutBuf) -> i32 {
            unsafe {
                if let Some((_, tim_ele_offset, beacon_interval)) = (*(device as *mut Self)).bcn_cfg
                {
                    (*(device as *mut Self)).bcn_cfg =
                        Some((buf.as_slice().to_vec(), tim_ele_offset, beacon_interval));
                    buf.free();
                    zx::sys::ZX_OK
                } else {
                    zx::sys::ZX_ERR_BAD_STATE
                }
            }
        }

        pub extern "C" fn configure_assoc(device: *mut c_void, cfg: *mut WlanAssocCtx) -> i32 {
            unsafe {
                (*(device as *mut Self)).assocs.insert((*cfg).bssid, (*cfg).clone());
            }
            zx::sys::ZX_OK
        }

        pub extern "C" fn clear_assoc(device: *mut c_void, addr: &MacAddr) -> i32 {
            unsafe {
                (*(device as *mut Self)).assocs.remove(addr);
            }
            zx::sys::ZX_OK
        }

        pub fn next_mlme_msg<T: fidl::encoding::Decodable>(&mut self) -> Result<T, Error> {
            use fidl::encoding::{decode_transaction_header, Decodable, Decoder};

            let mut buf = zx::MessageBuf::new();
            let () =
                self.sme_sap.1.read(&mut buf).map_err(|status| {
                    Error::Status(format!("error reading MLME message"), status)
                })?;

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
                start_hw_scan: Self::start_hw_scan,
                get_wlan_info: Self::get_wlan_info,
                configure_bss: Self::configure_bss,
                enable_beaconing: Self::enable_beaconing,
                disable_beaconing: Self::disable_beaconing,
                configure_beacon: Self::configure_beacon,
                set_link_status: Self::set_link_status,
                configure_assoc: Self::configure_assoc,
                clear_assoc: Self::clear_assoc,
            }
        }

        pub fn as_device_fail_wlan_tx(&mut self) -> Device {
            let mut dev = self.as_device();
            dev.send_wlan_frame = Self::send_wlan_frame_with_failure;
            dev
        }

        pub fn as_device_fail_start_hw_scan(&mut self) -> Device {
            Device { start_hw_scan: Self::start_hw_scan_fails, ..self.as_device() }
        }
    }

    pub fn fake_wlanmac_info() -> WlanmacInfo {
        let bands_count = 2;
        let mut bands = [default_band_info(); WLAN_INFO_MAX_BANDS];
        bands[0] = WlanInfoBandInfo {
            band: WlanInfoBand::_2GHZ,
            rates: arr!([12, 24, 48, 54, 96, 108], WLAN_INFO_BAND_INFO_MAX_RATES),
            supported_channels: WlanInfoChannelList {
                base_freq: 2407,
                channels: arr!(
                    [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
                    WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS
                ),
            },
            ht_supported: true,
            ht_caps: ht_cap(),
            vht_supported: false,
            vht_caps: Ieee80211VhtCapabilities {
                vht_capability_info: 0,
                supported_vht_mcs_and_nss_set: 0,
            },
        };
        bands[1] = WlanInfoBandInfo {
            band: WlanInfoBand::_5GHZ,
            rates: arr!([12, 24, 48, 54, 96, 108], WLAN_INFO_BAND_INFO_MAX_RATES),
            supported_channels: WlanInfoChannelList {
                base_freq: 5000,
                channels: arr!(
                    [36, 40, 44, 48, 149, 153, 157, 161],
                    WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS
                ),
            },
            ht_supported: true,
            ht_caps: ht_cap(),
            vht_supported: false,
            vht_caps: Ieee80211VhtCapabilities {
                vht_capability_info: 0x0f805032,
                supported_vht_mcs_and_nss_set: 0x0000fffe0000fffe,
            },
        };

        let ifc_info = WlanInfo {
            mac_addr: [7u8; 6],
            mac_role: WlanInfoMacRole::CLIENT,
            supported_phys: WlanInfoPhyType::OFDM | WlanInfoPhyType::HT | WlanInfoPhyType::VHT,
            driver_features: WlanInfoDriverFeature(0),
            caps: WlanInfoHardwareCapability(0),
            bands,
            bands_count,
        };
        WlanmacInfo { ifc_info }
    }

    fn ht_cap() -> Ieee80211HtCapabilities {
        Ieee80211HtCapabilities {
            ht_capability_info: 0x0063,
            ampdu_params: 0x17,
            supported_mcs_set: Ieee80211HtCapabilitiesSupportedMcsSet {
                bytes: [
                    // Rx MCS bitmask, Supported MCS values: 0-7
                    0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    // Tx parameters
                    0x01, 0x00, 0x00, 0x00,
                ],
            },
            ht_ext_capabilities: 0,
            tx_beamforming_capabilities: 0,
            asel_capabilities: 0,
        }
    }

    /// Placeholder for default initialization of WlanInfoBandInfo, used for case where we don't
    /// care about the exact information, but the type demands it.
    pub const fn default_band_info() -> WlanInfoBandInfo {
        WlanInfoBandInfo {
            band: WlanInfoBand(0),
            ht_supported: false,
            ht_caps: Ieee80211HtCapabilities {
                ht_capability_info: 0,
                ampdu_params: 0,
                supported_mcs_set: Ieee80211HtCapabilitiesSupportedMcsSet { bytes: [0; 16] },
                ht_ext_capabilities: 0,
                tx_beamforming_capabilities: 0,
                asel_capabilities: 0,
            },
            vht_supported: false,
            vht_caps: Ieee80211VhtCapabilities {
                vht_capability_info: 0,
                supported_vht_mcs_and_nss_set: 0,
            },
            rates: [0; WLAN_INFO_BAND_INFO_MAX_RATES],
            supported_channels: WlanInfoChannelList {
                base_freq: 0,
                channels: [0; WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS],
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::ddk_converter, banjo_ddk_hw_wlan_ieee80211::*,
        banjo_ddk_hw_wlan_wlaninfo::*, banjo_ddk_protocol_wlan_mac::WlanHwScanType,
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

        let mut fake_device = FakeDevice::new();
        let mut dev = fake_device.as_device();
        dev.get_sme_channel = get_sme_channel;
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
    fn start_hw_scan() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();

        let result = dev.start_hw_scan(&WlanHwScanConfig {
            scan_type: WlanHwScanType::PASSIVE,
            num_channels: 3,
            channels: arr!([1, 2, 3], WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS),
            ssid: WlanSsid { len: 3, ssid: arr!([65; 3], WLAN_MAX_SSID_LEN as usize) },
        });
        assert!(result.is_ok());
        assert_variant!(fake_device.hw_scan_req, Some(config) => {
            assert_eq!(config.scan_type, WlanHwScanType::PASSIVE);
            assert_eq!(config.num_channels, 3);
            assert_eq!(&config.channels[..], &arr!([1, 2, 3], WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS)[..]);
            assert_eq!(config.ssid, WlanSsid { len: 3, ssid: arr!([65; 3], WLAN_MAX_SSID_LEN as usize) });
        }, "expected HW scan config");
    }

    #[test]
    fn get_wlan_info() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();
        let info = dev.wlan_info();
        assert_eq!(info.ifc_info.mac_addr, [7u8; 6]);
        assert_eq!(info.ifc_info.bands_count, 2);
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
        .expect("error configuring bss");
        assert!(fake_device.bss_cfg.is_some());
    }

    #[test]
    fn enable_disable_beaconing() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();

        let mut in_buf = fake_device.buffer_provider.get_buffer(4).expect("error getting buffer");
        in_buf.as_mut_slice().copy_from_slice(&[1, 2, 3, 4][..]);

        dev.enable_beaconing(OutBuf::from(in_buf, 4), 1, TimeUnit(2))
            .expect("error enabling beaconing");
        assert_variant!(
        fake_device.bcn_cfg.as_ref(),
        Some((buf, tim_ele_offset, beacon_interval)) => {
            assert_eq!(&buf[..], &[1, 2, 3, 4][..]);
            assert_eq!(*tim_ele_offset, 1);
            assert_eq!(*beacon_interval, TimeUnit(2));
        });
        dev.disable_beaconing().expect("error disabling beaconing");
        assert_variant!(fake_device.bcn_cfg.as_ref(), None);
    }

    #[test]
    fn configure_beacon() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();

        {
            let mut in_buf =
                fake_device.buffer_provider.get_buffer(4).expect("error getting buffer");
            in_buf.as_mut_slice().copy_from_slice(&[1, 2, 3, 4][..]);
            dev.enable_beaconing(OutBuf::from(in_buf, 4), 1, TimeUnit(2))
                .expect("error enabling beaconing");
            assert_variant!(fake_device.bcn_cfg.as_ref(), Some((buf, _, _)) => {
                assert_eq!(&buf[..], &[1, 2, 3, 4][..]);
            });
        }

        {
            let mut in_buf =
                fake_device.buffer_provider.get_buffer(4).expect("error getting buffer");
            in_buf.as_mut_slice().copy_from_slice(&[1, 2, 3, 5][..]);
            dev.configure_beacon(OutBuf::from(in_buf, 4)).expect("error enabling beaconing");
            assert_variant!(fake_device.bcn_cfg.as_ref(), Some((buf, _, _)) => {
                assert_eq!(&buf[..], &[1, 2, 3, 5][..]);
            });
        }
    }

    #[test]
    fn set_link_status() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();

        dev.set_eth_link_up().expect("failed setting status");
        assert_eq!(fake_device.link_status, LinkStatus::UP);

        dev.set_eth_link_down().expect("failed setting status");
        assert_eq!(fake_device.link_status, LinkStatus::DOWN);
    }

    #[test]
    fn configure_assoc() {
        let mut fake_device = FakeDevice::new();
        let dev = fake_device.as_device();
        dev.configure_assoc(WlanAssocCtx {
            bssid: [1, 2, 3, 4, 5, 6],
            aid: 1,
            listen_interval: 2,
            phy: WlanPhyType::ERP,
            chan: WlanChannel { primary: 3, cbw: WlanChannelBandwidth::_20, secondary80: 0 },
            qos: false,
            ac_be_params: ddk_converter::blank_wmm_params(),
            ac_bk_params: ddk_converter::blank_wmm_params(),
            ac_vi_params: ddk_converter::blank_wmm_params(),
            ac_vo_params: ddk_converter::blank_wmm_params(),

            rates_cnt: 4,
            rates: [0; WLAN_MAC_MAX_RATES as usize],
            cap_info: 0x0102,

            has_ht_cap: false,
            // Safe: This is not read by the driver.
            ht_cap: unsafe { std::mem::zeroed::<Ieee80211HtCapabilities>() },
            has_ht_op: false,
            // Safe: This is not read by the driver.
            ht_op: unsafe { std::mem::zeroed::<WlanHtOp>() },

            has_vht_cap: false,
            // Safe: This is not read by the driver.
            vht_cap: unsafe { std::mem::zeroed::<Ieee80211VhtCapabilities>() },
            has_vht_op: false,
            // Safe: This is not read by the driver.
            vht_op: unsafe { std::mem::zeroed::<WlanVhtOp>() },
        })
        .expect("error configuring assoc");
        assert!(fake_device.assocs.contains_key(&[1, 2, 3, 4, 5, 6]));
    }
}
