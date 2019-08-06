// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod utils;

use {
    crate::{
        buffer::{BufferProvider, OutBuf},
        device::{Device, TxFlags},
        error::Error,
    },
    fuchsia_zircon as zx,
    log::error,
    std::ffi::c_void,
    wlan_common::{
        appendable::Appendable,
        big_endian::BigEndianU16,
        buffer_writer::BufferWriter,
        frame_len,
        mac::{self, OptionalField, Presence},
        sequence::SequenceManager,
    },
    zerocopy::ByteSlice,
};

pub use utils::*;

type MacAddr = [u8; 6];

/// A STA running in Client mode.
/// The Client STA is in its early development process and does not yet manage its internal state
/// machine or track negotiated capabilities.
pub struct ClientStation {
    device: Device,
    buf_provider: BufferProvider,
    seq_mgr: SequenceManager,
    bssid: MacAddr,
    iface_mac: MacAddr,
}

impl ClientStation {
    pub fn new(
        device: Device,
        buf_provider: BufferProvider,
        bssid: MacAddr,
        iface_mac: MacAddr,
    ) -> Self {
        Self { device, buf_provider, seq_mgr: SequenceManager::new(), bssid, iface_mac }
    }

    /// Returns a reference to the STA's SNS manager.
    pub fn seq_mgr(&mut self) -> &mut SequenceManager {
        &mut self.seq_mgr
    }

    /// Extracts aggregated and non-aggregated MSDUs from the data frame,
    /// converts those into Ethernet II frames and delivers the resulting frames via the given
    /// device.
    pub fn handle_data_frame<B: ByteSlice>(&mut self, bytes: B, has_padding: bool) {
        if let Some(msdus) = mac::MsduIterator::from_raw_data_frame(bytes, has_padding) {
            match msdus {
                mac::MsduIterator::Null => {}
                _ => {
                    for msdu in msdus {
                        if let Err(e) = self.deliver_msdu(msdu) {
                            error!("error while handling data frame: {}", e);
                        }
                    }
                }
            }
        }
    }

    /// Delivers a single MSDU to the STA's underlying device. The MSDU is delivered as an
    /// Ethernet II frame.
    /// Returns Err(_) if writing or delivering the Ethernet II frame failed.
    fn deliver_msdu<B: ByteSlice>(&mut self, msdu: mac::Msdu<B>) -> Result<(), Error> {
        let mac::Msdu { dst_addr, src_addr, llc_frame } = msdu;

        let mut buf = [0u8; mac::MAX_ETH_FRAME_LEN];
        let mut writer = BufferWriter::new(&mut buf[..]);
        write_eth_frame(
            &mut writer,
            dst_addr,
            src_addr,
            llc_frame.hdr.protocol_id.to_native(),
            &llc_frame.body,
        )?;
        self.device
            .deliver_eth_frame(writer.into_written())
            .map_err(|s| Error::Status(format!("could not deliver Ethernet II frame"), s))
    }

    /// Sends an authentication frame using Open System authentication.
    pub fn send_open_auth_frame(&mut self) -> Result<(), Error> {
        const FRAME_LEN: usize = frame_len!(mac::MgmtHdr, mac::AuthHdr);
        let mut buf = self.buf_provider.get_buffer(FRAME_LEN)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_open_auth_frame(&mut w, self.bssid, self.iface_mac, &mut self.seq_mgr)?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending open auth frame"), s))
    }

    /// Sends a "keep alive" response to the BSS. A keep alive response is a NULL data frame sent as
    /// a response to the AP transmitting NULL data frames to the client.
    // Note: This function was introduced to meet C++ MLME feature parity. However, there needs to
    // be some investigation, whether these "keep alive" frames are the right way of keeping a
    // client associated to legacy APs.
    pub fn send_keep_alive_resp_frame(&mut self) -> Result<(), Error> {
        const FRAME_LEN: usize = frame_len!(mac::FixedDataHdrFields);
        let mut buf = self.buf_provider.get_buffer(FRAME_LEN)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_keep_alive_resp_frame(&mut w, self.bssid, self.iface_mac, &mut self.seq_mgr)?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending keep alive frame"), s))
    }

    /// Sends a deauthentication notification to the joined BSS with the given `reason_code`.
    pub fn send_deauth_frame(&mut self, reason_code: mac::ReasonCode) -> Result<(), Error> {
        const FRAME_LEN: usize = frame_len!(mac::MgmtHdr, mac::DeauthHdr);
        let mut buf = self.buf_provider.get_buffer(FRAME_LEN)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_deauth_frame(&mut w, self.bssid, self.iface_mac, reason_code, &mut self.seq_mgr)?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending deauthenticate frame"), s))
    }

    /// Sends the given payload as a data frame over the air.
    pub fn send_data_frame(
        &mut self,
        src: MacAddr,
        dst: MacAddr,
        is_protected: bool,
        is_qos: bool,
        ether_type: u16,
        payload: &[u8],
    ) -> Result<(), Error> {
        let qos_presence = Presence::from_bool(is_qos);
        let data_hdr_len =
            mac::FixedDataHdrFields::len(mac::Addr4::ABSENT, qos_presence, mac::HtControl::ABSENT);
        let frame_len = data_hdr_len + std::mem::size_of::<mac::LlcHdr>() + payload.len();
        let mut buf = self.buf_provider.get_buffer(frame_len)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_data_frame(
            &mut w,
            &mut self.seq_mgr,
            self.bssid,
            src,
            dst,
            is_protected,
            is_qos,
            ether_type,
            payload,
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        let tx_flags = match ether_type {
            mac::ETHER_TYPE_EAPOL => TxFlags::FAVOR_RELIABILITY,
            _ => TxFlags::NONE,
        };
        self.device
            .send_wlan_frame(out_buf, tx_flags)
            .map_err(|s| Error::Status(format!("error sending data frame"), s))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{buffer::FakeBufferProvider, device::FakeDevice},
        wlan_common::test_utils::fake_frames::*,
    };
    const BSSID: MacAddr = [6u8; 6];
    const IFACE_MAC: MacAddr = [7u8; 6];

    fn make_client_station(device: Device) -> ClientStation {
        let buf_provider = FakeBufferProvider::new();
        let client = ClientStation::new(device, buf_provider, BSSID, IFACE_MAC);
        client
    }

    #[test]
    fn client_send_open_auth_frame() {
        let mut fake_device = FakeDevice::default();
        let mut client = make_client_station(fake_device.as_device());
        client.send_open_auth_frame().expect("error delivering WLAN frame");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Mgmt header:
            0b1011_00_00, 0b00000000, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // Sequence Control
            // Auth header:
            0, 0, // auth algorithm
            1, 0, // auth txn seq num
            0, 0, // status code
        ][..]);
    }

    #[test]
    fn client_send_keep_alive_resp_frame() {
        let mut fake_device = FakeDevice::default();
        let mut client = make_client_station(fake_device.as_device());
        client.send_keep_alive_resp_frame().expect("error delivering WLAN frame");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Data header:
            0b0100_10_00, 0b0000000_1, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // Sequence Control
        ][..]);
    }

    #[test]
    fn client_send_data_frame() {
        let payload = vec![5; 8];
        let mut fake_device = FakeDevice::default();
        let mut client = make_client_station(fake_device.as_device());
        client
            .send_data_frame([2; 6], [3; 6], false, false, 0x1234, &payload[..])
            .expect("error delivering WLAN frame");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Data header:
            0b0000_10_00, 0b0000000_1, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            3, 3, 3, 3, 3, 3, // addr3
            0x10, 0, // Sequence Control
            // LLC header:
            0xAA, 0xAA, 0x03, // DSAP, SSAP, Control
            0, 0, 0, // OUI
            0x12, 0x34, // Protocol ID
            // Payload
            5, 5, 5, 5, 5, 5, 5, 5,
        ][..]);
    }

    #[test]
    fn client_send_deauthentication_notification() {
        let mut fake_device = FakeDevice::default();
        let mut client = make_client_station(fake_device.as_device());
        client
            .send_deauth_frame(mac::ReasonCode::AP_INITIATED)
            .expect("error delivering WLAN frame");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Mgmt header:
            0b1100_00_00, 0b00000000, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // Sequence Control
            47, 0, // reason code
        ][..]);
    }

    #[test]
    fn data_frame_to_ethernet_single_llc() {
        let data_frame = make_data_frame_single_llc(None, None);
        let mut fake_device = FakeDevice::default();
        let mut client = make_client_station(fake_device.as_device());
        client.handle_data_frame(&data_frame[..], false);
        assert_eq!(fake_device.eth_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(fake_device.eth_queue[0], [
            3, 3, 3, 3, 3, 3, // dst_addr
            4, 4, 4, 4, 4, 4, // src_addr
            9, 10, // ether_type
            11, 11, 11, // payload
        ]);
    }

    #[test]
    fn data_frame_to_ethernet_amsdu() {
        let data_frame = make_data_frame_amsdu();
        let mut fake_device = FakeDevice::default();
        let mut client = make_client_station(fake_device.as_device());
        client.handle_data_frame(&data_frame[..], false);
        let queue = &fake_device.eth_queue;
        assert_eq!(queue.len(), 2);
        #[rustfmt::skip]
        let mut expected_first_eth_frame = vec![
            0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03, // dst_addr
            0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab, // src_addr
            0x08, 0x00, // ether_type
        ];
        expected_first_eth_frame.extend_from_slice(MSDU_1_PAYLOAD);
        assert_eq!(queue[0], &expected_first_eth_frame[..]);
        #[rustfmt::skip]
        let mut expected_second_eth_frame = vec![
            0x78, 0x8a, 0x20, 0x0d, 0x67, 0x04, // dst_addr
            0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xac, // src_addr
            0x08, 0x01, // ether_type
        ];
        expected_second_eth_frame.extend_from_slice(MSDU_2_PAYLOAD);
        assert_eq!(queue[1], &expected_second_eth_frame[..]);
    }

    #[test]
    fn data_frame_to_ethernet_amsdu_padding_too_short() {
        let data_frame = make_data_frame_amsdu_padding_too_short();
        let mut fake_device = FakeDevice::default();
        let mut client = make_client_station(fake_device.as_device());
        client.handle_data_frame(&data_frame[..], false);
        let queue = &fake_device.eth_queue;
        assert_eq!(queue.len(), 1);
        #[rustfmt::skip]
        let mut expected_first_eth_frame = vec![
            0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03, // dst_addr
            0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab, // src_addr
            0x08, 0x00, // ether_type
        ];
        expected_first_eth_frame.extend_from_slice(MSDU_1_PAYLOAD);
        assert_eq!(queue[0], &expected_first_eth_frame[..]);
    }
}
