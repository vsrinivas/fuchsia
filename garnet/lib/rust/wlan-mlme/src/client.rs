// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::auth,
    failure::Error,
    std::{ops::Deref, ops::DerefMut},
    wlan_common::{
        buffer_writer::{BufferWriter, ByteSliceMut, LayoutVerified},
        data_writer,
        mac::{self, OptionalField},
        mgmt_writer,
        sequence::SequenceManager,
    },
};

type MacAddr = [u8; 6];

pub fn write_open_auth_frame<B: ByteSliceMut>(
    buf: B,
    bssid: MacAddr,
    client_addr: MacAddr,
    seq_mgr: &mut SequenceManager,
) -> Result<BufferWriter<B>, Error> {
    let mut frame_ctrl = mac::FrameControl(0);
    frame_ctrl.set_frame_subtype(mac::MGMT_SUBTYPE_AUTH);
    let mut seq_ctrl = mac::SequenceControl(0);
    seq_ctrl.set_seq_num(seq_mgr.next_sns1(&bssid) as u16);
    let mut w = mgmt_writer::write_mgmt_hdr(
        BufferWriter::new(buf),
        mgmt_writer::FixedFields::sent_from_client(
            frame_ctrl,
            bssid,
            client_addr,
            seq_ctrl.value(),
        ),
        None,
    )?;

    let (mut auth_hdr, w) = w.reserve_zeroed::<mac::AuthHdr>()?;
    auth::write_client_req(&mut auth_hdr);
    Ok(w)
}

pub fn write_deauth_frame<B: ByteSliceMut>(
    buf: B,
    bssid: MacAddr,
    client_addr: MacAddr,
    reason_code: mac::ReasonCode,
    seq_mgr: &mut SequenceManager,
) -> Result<BufferWriter<B>, Error> {
    let mut frame_ctrl = mac::FrameControl(0);
    frame_ctrl.set_frame_subtype(mac::MGMT_SUBTYPE_DEAUTH);
    let mut seq_ctrl = mac::SequenceControl(0);
    seq_ctrl.set_seq_num(seq_mgr.next_sns1(&bssid) as u16);
    let mut w = mgmt_writer::write_mgmt_hdr(
        BufferWriter::new(buf),
        mgmt_writer::FixedFields::sent_from_client(
            frame_ctrl,
            bssid,
            client_addr,
            seq_ctrl.value(),
        ),
        None,
    )?;

    let (mut deauth_hdr, w) = w.reserve_zeroed::<mac::DeauthHdr>()?;
    deauth_hdr.set_reason_code(reason_code as u16);
    Ok(w)
}

/// Fills a given buffer with a null-data frame.
pub fn write_keep_alive_resp_frame<B: ByteSliceMut>(
    buf: B,
    bssid: MacAddr,
    client_addr: MacAddr,
    seq_mgr: &mut SequenceManager,
) -> Result<BufferWriter<B>, Error> {
    let mut frame_ctrl = mac::FrameControl(0);
    frame_ctrl.set_frame_subtype(mac::DATA_SUBTYPE_NULL_DATA);
    let mut seq_ctrl = mac::SequenceControl(0);
    seq_ctrl.set_seq_num(seq_mgr.next_sns1(&bssid) as u16);
    let w = data_writer::write_data_hdr(
        BufferWriter::new(buf),
        data_writer::FixedFields::sent_from_client(
            frame_ctrl,
            bssid,
            client_addr,
            seq_ctrl.value(),
        ),
        data_writer::OptionalFields::none(),
    )?;
    Ok(w)
}

pub fn write_eth_frame<B: ByteSliceMut>(
    buf: B,
    dst_addr: MacAddr,
    src_addr: MacAddr,
    protocol_id: u16,
    body: &[u8],
) -> Result<BufferWriter<B>, Error> {
    let (mut eth_hdr, w) = BufferWriter::new(buf).reserve_zeroed::<mac::EthernetIIHdr>()?;
    eth_hdr.da = dst_addr;
    eth_hdr.sa = src_addr;
    eth_hdr.set_ether_type(protocol_id);

    let w = w.write_bytes(body)?;
    Ok(w)
}

pub fn write_eapol_frame<B: ByteSliceMut>(
    buf: B,
    dest: MacAddr,
    src: MacAddr,
    seq_num: u16,
    protected: bool,
    eapol_frame: &[u8],
) -> Result<usize, Error> {
    let mut frame_ctrl = mac::FrameControl(0);
    frame_ctrl.set_frame_subtype(mac::DATA_SUBTYPE_DATA);
    frame_ctrl.set_protected(protected);
    let mut seq_ctrl = mac::SequenceControl(0);
    seq_ctrl.set_seq_num(seq_num);
    let w = data_writer::write_data_hdr(
        BufferWriter::new(buf),
        data_writer::FixedFields::sent_from_client(frame_ctrl, dest, src, seq_ctrl.value()),
        data_writer::OptionalFields::none(),
    )?;

    let w = data_writer::write_snap_llc_hdr(w, mac::ETHER_TYPE_EAPOL)?;
    let w = w.write_bytes(eapol_frame)?;
    Ok(w.written_bytes())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn open_auth_frame() {
        let mut buf = [99u8; 30];
        let mut seq_mgr = SequenceManager::new();
        let written_bytes = write_open_auth_frame(&mut buf[..], [1; 6], [2; 6], &mut seq_mgr)
            .expect("failed writing frame")
            .written_bytes();
        assert_eq!(30, written_bytes);
        assert_eq!(
            [
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
            buf
        );
    }

    #[test]
    fn deauth_frame() {
        let mut buf = [99u8; 26];
        let mut seq_mgr = SequenceManager::new();
        let written_bytes = write_deauth_frame(
            &mut buf[..],
            [1; 6],
            [2; 6],
            mac::ReasonCode::Timeout,
            &mut seq_mgr,
        )
        .expect("failed writing frame")
        .written_bytes();
        assert_eq!(26, written_bytes);
        assert_eq!(
            [
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
            buf
        );
    }

    #[test]
    fn keep_alive_resp_frame() {
        let mut buf = [3u8; 25];
        let mut seq_mgr = SequenceManager::new();
        let written_bytes = write_keep_alive_resp_frame(&mut buf[..], [1; 6], [2; 6], &mut seq_mgr)
            .expect("failed writing frame")
            .written_bytes();
        assert_eq!(24, written_bytes);
        assert_eq!(
            [
                0b01001000, 0b1, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                1, 1, 1, 1, 1, 1, // addr3
                0, 0, // Sequence Control
                3  // Trailing bytes
            ],
            buf
        );
    }

    #[test]
    fn eth_frame_ok() {
        let mut buf = [7u8; 25];
        let written_bytes = write_eth_frame(&mut buf[..], [1; 6], [2; 6], 3333, &[4; 9])
            .expect("failed writing ethernet frame")
            .written_bytes();
        assert_eq!(23, written_bytes);
        assert_eq!(
            [
                1, 1, 1, 1, 1, 1, // dst_addr
                2, 2, 2, 2, 2, 2, // src_addr
                0x0d, 0x05, // ether_type
                4, 4, 4, 4, 4, 4, 4, 4, // payload
                4, // more payload
                7, 7, // untouched bytes
            ],
            buf
        );
    }

    #[test]
    fn eth_frame_buffer_too_small() {
        let mut buf = [7u8; 22];
        let write_result = write_eth_frame(&mut buf[..], [1; 6], [2; 6], 3333, &[4; 9]);
        assert!(write_result.is_err());
    }

    #[test]
    fn eth_frame_empty_payload() {
        let mut buf = [7u8; 16];
        let written_bytes = write_eth_frame(&mut buf[..], [1; 6], [2; 6], 3333, &[])
            .expect("failed writing ethernet frame")
            .written_bytes();
        assert_eq!(14, written_bytes);
        assert_eq!(
            [
                1, 1, 1, 1, 1, 1, // dst_addr
                2, 2, 2, 2, 2, 2, // src_addrfx
                0x0d, 0x05, // ether_type
                7, 7, // untouched bytes
            ],
            buf
        );
    }

    #[test]
    fn eapol_frame() {
        let mut buf = [99u8; 35];
        let written_bytes = write_eapol_frame(&mut buf[..], [1; 6], [2; 6], 42, true, &[4, 5, 6])
            .expect("failed writing frame");
        assert_eq!(35, written_bytes);
        let expected = [
            // Data header
            0b00001000, 0b01000001, // Frame Control
            0, 0, // Duration
            1, 1, 1, 1, 1, 1, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            1, 1, 1, 1, 1, 1, // addr3
            0b10100000, 0b10, // Sequence Control
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
    fn eapol_frame_empty_payload() {
        let mut buf = [99u8; 32];
        let written_bytes = write_eapol_frame(&mut buf[..], [1; 6], [2; 6], 42, true, &[])
            .expect("failed writing frame");
        assert_eq!(32, written_bytes);
        let expected = [
            // Data header
            0b00001000, 0b01000001, // Frame Control
            0, 0, // Duration
            1, 1, 1, 1, 1, 1, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            1, 1, 1, 1, 1, 1, // addr3
            0b10100000, 0b10, // Sequence Control
            // LLC header
            0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
            0, 0, 0, // OUI
            0x88, 0x8E, // Protocol ID
        ];
    }

    #[test]
    fn eapol_frame_buffer_too_small() {
        let mut buf = [99u8; 34];
        let result = write_eapol_frame(&mut buf[..], [1; 6], [2; 6], 42, true, &[4, 5, 6]);
        assert!(result.is_err(), "expect writing eapol frame to fail");
    }
}
