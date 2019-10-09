// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{auth, error::Error},
    wlan_common::{
        appendable::Appendable,
        mac::{self, Bssid, MacAddr, StatusCode},
        mgmt_writer,
        sequence::SequenceManager,
    },
};

#[allow(unused)]
pub fn write_open_auth_frame<B: Appendable>(
    buf: &mut B,
    client_addr: MacAddr,
    bssid: Bssid,
    seq_mgr: &mut SequenceManager,
    status_code: StatusCode,
) -> Result<(), Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::AUTH);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns1(&client_addr) as u16);
    mgmt_writer::write_mgmt_hdr(
        buf,
        mgmt_writer::mgmt_hdr_from_ap(frame_ctrl, client_addr, bssid, seq_ctrl),
        None,
    )?;

    buf.append_value(&auth::make_open_ap_resp(status_code))?;
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn open_auth_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_open_auth_frame(
            &mut buf,
            [1; 6],
            Bssid([2; 6]),
            &mut seq_mgr,
            StatusCode::TRANSACTION_SEQUENCE_ERROR,
        )
        .expect("failed writing frame");
        assert_eq!(
            &[
                // Mgmt header
                0b10110000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Auth body
                0, 0, // Auth Algorithm Number
                2, 0, // Auth Txn Seq Number
                14, 0, // Status code
            ],
            &buf[..]
        );
    }
}
