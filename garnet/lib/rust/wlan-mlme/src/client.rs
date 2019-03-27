// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{auth, device},
    failure::Error,
    fuchsia_zircon::sys as zx,
    std::{ops::Deref, ops::DerefMut},
    wlan_common::{
        appendable::Appendable,
        buffer_writer::BufferWriter,
        data_writer,
        mac::{self, OptionalField},
        mgmt_writer,
        sequence::SequenceManager,
    },
    zerocopy::ByteSlice,
};

type MacAddr = [u8; 6];

pub fn write_open_auth_frame<B: Appendable>(
    buf: &mut B,
    bssid: MacAddr,
    client_addr: MacAddr,
    seq_mgr: &mut SequenceManager,
) -> Result<(), Error> {
    let mut frame_ctrl = mac::FrameControl(0);
    frame_ctrl.set_frame_subtype(mac::MGMT_SUBTYPE_AUTH);
    let mut seq_ctrl = mac::SequenceControl(0);
    seq_ctrl.set_seq_num(seq_mgr.next_sns1(&bssid) as u16);
    mgmt_writer::write_mgmt_hdr(
        buf,
        mgmt_writer::mgmt_hdr_client_to_ap(frame_ctrl, bssid, client_addr, seq_ctrl),
        None,
    )?;

    let mut auth_hdr = buf.append_value_zeroed::<mac::AuthHdr>()?;
    auth::write_client_req(&mut auth_hdr);
    Ok(())
}

pub fn write_deauth_frame<B: Appendable>(
    buf: &mut B,
    bssid: MacAddr,
    client_addr: MacAddr,
    reason_code: mac::ReasonCode,
    seq_mgr: &mut SequenceManager,
) -> Result<(), Error> {
    let mut frame_ctrl = mac::FrameControl(0);
    frame_ctrl.set_frame_subtype(mac::MGMT_SUBTYPE_DEAUTH);
    let mut seq_ctrl = mac::SequenceControl(0);
    seq_ctrl.set_seq_num(seq_mgr.next_sns1(&bssid) as u16);
    mgmt_writer::write_mgmt_hdr(
        buf,
        mgmt_writer::mgmt_hdr_client_to_ap(frame_ctrl, bssid, client_addr, seq_ctrl),
        None,
    )?;

    let mut deauth_hdr = buf.append_value_zeroed::<mac::DeauthHdr>()?;
    deauth_hdr.set_reason_code(reason_code as u16);
    Ok(())
}

/// Fills a given buffer with a null-data frame.
pub fn write_keep_alive_resp_frame<B: Appendable>(
    buf: &mut B,
    bssid: MacAddr,
    client_addr: MacAddr,
    seq_mgr: &mut SequenceManager,
) -> Result<(), Error> {
    let mut frame_ctrl = mac::FrameControl(0);
    frame_ctrl.set_frame_subtype(mac::DATA_SUBTYPE_NULL_DATA);
    let mut seq_ctrl = mac::SequenceControl(0);
    seq_ctrl.set_seq_num(seq_mgr.next_sns1(&bssid) as u16);
    data_writer::write_data_hdr(
        buf,
        data_writer::data_hdr_client_to_ap(frame_ctrl, bssid, client_addr, seq_ctrl),
        mac::OptionalDataHdrFields::none(),
    )?;
    Ok(())
}

/// Extracts aggregated and non-aggregated MSDUs from the data frame, converts those into Ethernet frames and delivers those frames via the given device.
pub fn handle_data_frame<B: ByteSlice>(device: &device::Device, bytes: B, has_padding: bool) {
    if let Some(msdus) = mac::MsduIterator::from_raw_data_frame(bytes, has_padding) {
        deliver_msdus(device, msdus);
    }
}

/// Delivers MSDUs parsed from a data frame to the underlying device.
pub fn deliver_msdus<B: ByteSlice>(device: &device::Device, msdus: mac::MsduIterator<B>) {
    let mut buf = [0u8; mac::MAX_ETH_FRAME_LEN];
    for mac::Msdu { dst_addr, src_addr, llc_frame } in msdus {
        let mut writer = BufferWriter::new(&mut buf[..]);
        let write_result = write_eth_frame(
            &mut writer,
            dst_addr,
            src_addr,
            llc_frame.hdr.protocol_id.to_native(),
            &llc_frame.body,
        );
        match write_result {
            Ok(_) => {
                if let Err(e) = device.deliver_ethernet(writer.into_written()) {
                    println!("error delivering ethernet frame: {}", e);
                }
            }
            Err(e) => {
                println!("error writing ethernet frame with len {}: {}", llc_frame.body.len(), e);
            }
        }
    }
}

pub fn write_eth_frame<B: Appendable>(
    buf: &mut B,
    dst_addr: MacAddr,
    src_addr: MacAddr,
    protocol_id: u16,
    body: &[u8],
) -> Result<(), Error> {
    let mut eth_hdr = buf.append_value_zeroed::<mac::EthernetIIHdr>()?;
    eth_hdr.da = dst_addr;
    eth_hdr.sa = src_addr;
    eth_hdr.ether_type.set_from_native(protocol_id);

    buf.append_bytes(body)?;
    Ok(())
}

pub fn write_eapol_data_frame<B: Appendable>(
    buf: &mut B,
    dest: MacAddr,
    src: MacAddr,
    seq_mgr: &mut SequenceManager,
    protected: bool,
    eapol_frame: &[u8],
) -> Result<(), Error> {
    let mut frame_ctrl = mac::FrameControl(0);
    frame_ctrl.set_frame_subtype(mac::DATA_SUBTYPE_DATA);
    frame_ctrl.set_protected(protected);
    let mut seq_ctrl = mac::SequenceControl(0);
    seq_ctrl.set_seq_num(seq_mgr.next_sns1(&dest) as u16);
    data_writer::write_data_hdr(
        buf,
        data_writer::data_hdr_client_to_ap(frame_ctrl, dest, src, seq_ctrl),
        mac::OptionalDataHdrFields::none(),
    )?;

    data_writer::write_snap_llc_hdr(buf, mac::ETHER_TYPE_EAPOL)?;
    buf.append_bytes(eapol_frame)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, crate::device::FakeDevice, wlan_common::test_utils::fake_frames::*};

    #[test]
    fn open_auth_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_open_auth_frame(&mut buf, [1; 6], [2; 6], &mut seq_mgr)
            .expect("failed writing frame");
        assert_eq!(
            &[
                // Mgmt header
                0b10110000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                1, 1, 1, 1, 1, 1, // addr3
                0, 0, // Sequence Control
                // Auth body
                0, 0, // Auth Algorithm Number
                1, 0, // Auth Txn Seq Number
                0, 0, // Status code
            ],
            &buf[..]
        );
    }

    #[test]
    fn deauth_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_deauth_frame(&mut buf, [1; 6], [2; 6], mac::ReasonCode::Timeout, &mut seq_mgr)
            .expect("failed writing frame");
        assert_eq!(
            &[
                // Mgmt header
                0b11000000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                1, 1, 1, 1, 1, 1, // addr3
                0, 0, // Sequence Control
                // Deauth body
                0x27, 0 // Reason code
            ],
            &buf[..]
        );
    }

    #[test]
    fn keep_alive_resp_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_keep_alive_resp_frame(&mut buf, [1; 6], [2; 6], &mut seq_mgr)
            .expect("failed writing frame");
        assert_eq!(
            &[
                0b01001000, 0b1, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                1, 1, 1, 1, 1, 1, // addr3
                0, 0, // Sequence Control
            ],
            &buf[..]
        );
    }

    #[test]
    fn eth_frame_ok() {
        let mut buf = vec![];
        write_eth_frame(&mut buf, [1; 6], [2; 6], 3333, &[4; 9])
            .expect("failed writing ethernet frame");
        assert_eq!(
            &[
                1, 1, 1, 1, 1, 1, // dst_addr
                2, 2, 2, 2, 2, 2, // src_addr
                0x0d, 0x05, // ether_type
                4, 4, 4, 4, 4, 4, 4, 4, // payload
                4, // more payload
            ],
            &buf[..]
        );
    }

    #[test]
    fn eth_frame_buffer_too_small() {
        let mut buf = [7u8; 22];
        let write_result =
            write_eth_frame(&mut BufferWriter::new(&mut buf[..]), [1; 6], [2; 6], 3333, &[4; 9]);
        assert!(write_result.is_err());
    }

    #[test]
    fn eth_frame_empty_payload() {
        let mut buf = vec![];
        write_eth_frame(&mut buf, [1; 6], [2; 6], 3333, &[])
            .expect("failed writing ethernet frame");
        assert_eq!(
            &[
                1, 1, 1, 1, 1, 1, // dst_addr
                2, 2, 2, 2, 2, 2, // src_addrfx
                0x0d, 0x05, // ether_type
            ],
            &buf[..]
        );
    }

    #[test]
    fn eapol_data_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_eapol_data_frame(&mut buf, [1; 6], [2; 6], &mut seq_mgr, true, &[4, 5, 6])
            .expect("failed writing frame");
        let expected = [
            // Data header
            0b00001000, 0b01000001, // Frame Control
            0, 0, // Duration
            1, 1, 1, 1, 1, 1, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            1, 1, 1, 1, 1, 1, // addr3
            0, 0, // Sequence Control
            // LLC header
            0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
            0, 0, 0, // OUI
            0x88, 0x8E, // Protocol ID
            // Payload
            4, 5, 6,
        ];
        assert_eq!(&expected[..], &buf[..]);
    }

    #[test]
    fn eapol_data_frame_empty_payload() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_eapol_data_frame(&mut buf, [1; 6], [2; 6], &mut seq_mgr, true, &[])
            .expect("failed writing frame");
        let expected = [
            // Data header
            0b00001000, 0b01000001, // Frame Control
            0, 0, // Duration
            1, 1, 1, 1, 1, 1, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            1, 1, 1, 1, 1, 1, // addr3
            0, 0, // Sequence Control
            // LLC header
            0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
            0, 0, 0, // OUI
            0x88, 0x8E, // Protocol ID
        ];
        assert_eq!(&expected[..], &buf[..]);
    }

    #[test]
    fn eapol_data_frame_buffer_too_small() {
        let mut buf = [99u8; 34];
        let mut seq_mgr = SequenceManager::new();
        let result = write_eapol_data_frame(
            &mut BufferWriter::new(&mut buf[..]),
            [1; 6],
            [2; 6],
            &mut seq_mgr,
            true,
            &[4, 5, 6],
        );
        assert!(result.is_err(), "expect writing eapol frame to fail");
    }

    #[test]
    fn data_frame_to_ethernet_single_llc() {
        let data_frame = make_data_frame_single_llc(None, None);
        let mut fake_device = FakeDevice::default();
        handle_data_frame(&fake_device.as_device(), &data_frame[..], false);
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
        handle_data_frame(&fake_device.as_device(), &data_frame[..], false);
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
        handle_data_frame(&fake_device.as_device(), &data_frame[..], false);
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
