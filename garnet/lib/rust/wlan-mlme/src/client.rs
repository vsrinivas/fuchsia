// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::auth,
    failure::Error,
    std::{ops::Deref, ops::DerefMut},
    wlan_common::{
        buffer_writer::{BufferWriter, ByteSliceMut, LayoutVerified},
        mac::{self, OptionalField},
        data_writer, mgmt_writer,
    },
};

type MacAddr = [u8; 6];

pub fn write_open_auth_frame<B: ByteSliceMut>(
    buf: B,
    bssid: MacAddr,
    client_addr: MacAddr,
    seq_ctrl: u16,
) -> Result<BufferWriter<B>, Error> {
    let mut frame_ctrl = mac::FrameControl(0);
    frame_ctrl.set_frame_subtype(mac::MGMT_SUBTYPE_AUTH);
    let mut w = mgmt_writer::write_mgmt_hdr(
        BufferWriter::new(buf),
        mgmt_writer::FixedFields::sent_from_client(frame_ctrl, bssid, client_addr, seq_ctrl),
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
    seq_ctrl: u16,
    reason_code: mac::ReasonCode,
) -> Result<BufferWriter<B>, Error> {
    let mut frame_ctrl = mac::FrameControl(0);
    frame_ctrl.set_frame_subtype(mac::MGMT_SUBTYPE_DEAUTH);
    let mut w = mgmt_writer::write_mgmt_hdr(
        BufferWriter::new(buf),
        mgmt_writer::FixedFields::sent_from_client(frame_ctrl, bssid, client_addr, seq_ctrl),
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
    seq_ctrl: u16,
) -> Result<BufferWriter<B>, Error> {
    let mut frame_ctrl = mac::FrameControl(0);
    frame_ctrl.set_frame_subtype(mac::DATA_SUBTYPE_NULL_DATA);
    let w = data_writer::write_data_hdr(
        BufferWriter::new(buf),
        data_writer::FixedFields::sent_from_client(frame_ctrl, bssid, client_addr, seq_ctrl),
        data_writer::OptionalFields::none(),
    )?;
    Ok(w)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn open_auth_frame() {
        let mut buf = [99u8; 30];
        let written_bytes = write_open_auth_frame(&mut buf[..], [1; 6], [2; 6], 42)
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
                42, 0, // Sequence Control
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
        let written_bytes =
            write_deauth_frame(&mut buf[..], [1; 6], [2; 6], 42, mac::ReasonCode::Timeout)
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
                42, 0, // Sequence Control
                // Deauth body
                0x27, 0 // Reason code
            ],
            buf
        );
    }

    #[test]
    fn keep_alive_resp_frame() {
        let mut buf = [3u8; 25];
        let written_bytes = write_keep_alive_resp_frame(&mut buf[..], [1; 6], [2; 6], 42)
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
                42, 0, // Sequence Control
                3  // Trailing bytes
            ],
            buf
        );
    }
}
