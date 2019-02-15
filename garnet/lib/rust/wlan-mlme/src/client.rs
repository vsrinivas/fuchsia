// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::auth,
    failure::Error,
    wlan_common::{
        buffer_writer::{BufferWriter, ByteSliceMut},
        mac,
    },
};

type MacAddr = [u8; 6];

pub fn write_open_auth_frame<B: ByteSliceMut>(
    buf: B,
    bssid: MacAddr,
    client_addr: MacAddr,
    seq_ctrl: u16,
) -> Result<BufferWriter<B>, Error> {
    let (mut mgmt_hdr, mut w) = BufferWriter::new(buf).reserve_zeroed::<mac::MgmtHdr>()?;
    let mut fc = mac::FrameControl(0);
    fc.set_frame_type(mac::FRAME_TYPE_MGMT);
    fc.set_frame_subtype(mac::MGMT_SUBTYPE_AUTH);
    mgmt_hdr.set_frame_ctrl(fc.value());
    mgmt_hdr.addr1 = bssid;
    mgmt_hdr.addr2 = client_addr;
    mgmt_hdr.addr3 = bssid;
    mgmt_hdr.set_seq_ctrl(seq_ctrl);

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
    let (mut mgmt_hdr, mut w) = BufferWriter::new(buf).reserve_zeroed::<mac::MgmtHdr>()?;
    let mut fc = mac::FrameControl(0);
    fc.set_frame_type(mac::FRAME_TYPE_MGMT);
    fc.set_frame_subtype(mac::MGMT_SUBTYPE_DEAUTH);
    mgmt_hdr.set_frame_ctrl(fc.value());
    mgmt_hdr.addr1 = bssid;
    mgmt_hdr.addr2 = client_addr;
    mgmt_hdr.addr3 = bssid;
    mgmt_hdr.set_seq_ctrl(seq_ctrl);

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
    let (mut data_hdr, w) = BufferWriter::new(buf).reserve_zeroed::<mac::DataHdr>()?;
    let mut fc = mac::FrameControl(0);
    fc.set_frame_type(mac::FRAME_TYPE_DATA);
    fc.set_frame_subtype(mac::DATA_SUBTYPE_NULL_DATA);
    fc.set_to_ds(true);
    data_hdr.set_frame_ctrl(fc.value());
    data_hdr.addr1 = bssid;
    data_hdr.addr2 = client_addr;
    data_hdr.addr3 = bssid;
    data_hdr.set_seq_ctrl(seq_ctrl);
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
