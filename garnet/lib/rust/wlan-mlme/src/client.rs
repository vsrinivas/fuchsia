// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    wlan_common::{
        buffer_writer::{BufferWriter, ByteSliceMut},
        mac,
    },
};

type MacAddr = [u8; 6];

/// Fills a given buffer with auth frame which will be sent from client to AP.
/// Fails if the given buffer is too short.
/// Returns the amount of bytes written to the buffer.
pub fn write_auth_frame<B: ByteSliceMut>(
    buf: B,
    bssid: MacAddr,
    client_addr: MacAddr,
    seq_ctrl: u16,
    auth_alg_num: mac::AuthAlgorithm,
) -> Result<usize, Error> {
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
    auth_hdr.set_auth_alg_num(auth_alg_num as u16);
    auth_hdr.set_auth_txn_seq_num(1);
    auth_hdr.set_status_code(0);
    Ok(w.written_bytes())
}

/// Fills a given buffer with a null-data frame which will be send from a client to an AP.
/// Fails if the given buffer is too short.
/// Returns the amount of bytes written to the buffer.
pub fn write_keep_alive_resp_frame<B: ByteSliceMut>(
    buf: B,
    bssid: MacAddr,
    client_addr: MacAddr,
    seq_ctrl: u16,
) -> Result<usize, Error> {
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
    Ok(w.written_bytes())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn auth_frame() {
        let mut buf = [99u8; 30];
        let written_bytes =
            write_auth_frame(&mut buf[..], [1; 6], [2; 6], 42, mac::AuthAlgorithm::Sae)
                .expect("failed writing frame");
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
                3, 0, // Auth Algorithm Number
                1, 0, // Auth Txn Seq Number
                0, 0, // Status code
            ],
            buf
        );
    }

    #[test]
    fn keep_alive_resp_frame() {
        let mut buf = [3u8; 25];
        let written_bytes = write_keep_alive_resp_frame(&mut buf[..], [1; 6], [2; 6], 42)
            .expect("failed writing frame");
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
