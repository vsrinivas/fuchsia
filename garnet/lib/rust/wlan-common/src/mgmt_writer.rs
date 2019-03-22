// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        appendable::Appendable,
        mac::{self, FrameControl, HtControl, MgmtHdr, SequenceControl},
    },
    failure::{ensure, Error},
};

type MacAddr = [u8; 6];

pub fn mgmt_hdr_client_to_ap(
    frame_ctrl: FrameControl,
    bssid: MacAddr,
    client_addr: MacAddr,
    seq_ctrl: SequenceControl,
) -> MgmtHdr {
    MgmtHdr { frame_ctrl, duration: 0, addr1: bssid, addr2: client_addr, addr3: bssid, seq_ctrl }
}

fn make_new_frame_ctrl(
    mut fc: FrameControl,
    ht_ctrl: Option<HtControl>,
) -> Result<FrameControl, Error> {
    fc.set_frame_type(mac::FRAME_TYPE_MGMT);
    if ht_ctrl.is_some() {
        fc.set_htc_order(true);
    } else {
        ensure!(!fc.htc_order(), "htc_order bit set while HT-Control is absent");
    }
    Ok(fc)
}

pub fn write_mgmt_hdr<B: Appendable>(
    w: &mut B,
    mut fixed: MgmtHdr,
    ht_ctrl: Option<HtControl>,
) -> Result<(), Error> {
    fixed.frame_ctrl = make_new_frame_ctrl(fixed.frame_ctrl, ht_ctrl)?;
    w.append_value(&fixed)?;
    if let Some(ht_ctrl) = ht_ctrl.as_ref() {
        w.append_value(ht_ctrl)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, crate::buffer_writer::BufferWriter};

    #[test]
    fn client_to_ap() {
        let got = mgmt_hdr_client_to_ap(FrameControl(1234), [1; 6], [2; 6], SequenceControl(4321));
        let expected = MgmtHdr {
            frame_ctrl: FrameControl(1234),
            duration: 0,
            addr1: [1; 6],
            addr2: [2; 6],
            addr3: [1; 6],
            seq_ctrl: SequenceControl(4321),
        };
        assert_eq!(got, expected);
    }

    #[test]
    fn too_small_buffer() {
        let mut bytes = vec![0u8; 20];
        let result = write_mgmt_hdr(
            &mut BufferWriter::new(&mut bytes[..]),
            MgmtHdr {
                frame_ctrl: FrameControl(0b00110001_00110000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            None,
        );
        assert!(result.is_err(), "expected failure when writing into too small buffer");
    }

    #[test]
    fn invalid_ht_configuration() {
        let mut bytes = vec![];
        let result = write_mgmt_hdr(
            &mut bytes,
            MgmtHdr {
                frame_ctrl: FrameControl(0b10110001_00110000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            None,
        );
        assert!(result.is_err(), "expected failure due to invalid ht configuration");
    }

    #[test]
    fn write_fixed_fields_only() {
        let mut bytes = vec![];
        write_mgmt_hdr(
            &mut bytes,
            MgmtHdr {
                frame_ctrl: FrameControl(0b00110001_00110000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            None,
        )
        .expect("Failed writing mgmt frame");

        #[rustfmt::skip]
        assert_eq!(
            &bytes[..],
            [
                // Data Header
                0b00110000u8, 0b00110001, // Frame Control
                0, 0, // duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                3, 3, 3, 3, 3, 3, // addr3
                0b10010000, 0b11000000, // Sequence Control
            ]
        );
    }

    #[test]
    fn write_ht_ctrl() {
        let mut bytes = vec![];
        write_mgmt_hdr(
            &mut bytes,
            MgmtHdr {
                frame_ctrl: FrameControl(0b00110001_00110000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            Some(HtControl(0b10101111_11000011_11110000_10101010)),
        )
        .expect("Failed writing mgmt frame");

        #[rustfmt::skip]
        assert_eq!(
            &bytes[..],
            &[
                // Data Header
                0b00110000u8, 0b10110001, // Frame Control
                0, 0, // duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                3, 3, 3, 3, 3, 3, // addr3
                0b10010000, 0b11000000, // Sequence Control
                // Ht Control
                0b10101010, 0b11110000, 0b11000011, 0b10101111,
            ][..]
        );
    }
}
