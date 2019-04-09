// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    appendable::Appendable,
    error::FrameWriteError,
    mac::{self, FrameControl, HtControl, MgmtHdr, SequenceControl},
};

type MacAddr = [u8; 6];

pub fn mgmt_hdr_to_ap(
    frame_ctrl: FrameControl,
    bssid: MacAddr,
    client_addr: MacAddr,
    seq_ctrl: SequenceControl,
) -> MgmtHdr {
    MgmtHdr { frame_ctrl, duration: 0, addr1: bssid, addr2: client_addr, addr3: bssid, seq_ctrl }
}

pub fn mgmt_hdr_from_ap(
    frame_ctrl: FrameControl,
    client_addr: MacAddr,
    bssid: MacAddr,
    seq_ctrl: SequenceControl,
) -> MgmtHdr {
    MgmtHdr { frame_ctrl, duration: 0, addr1: client_addr, addr2: bssid, addr3: bssid, seq_ctrl }
}

fn validate_frame_ctrl(fc: FrameControl, ht_ctrl: Option<HtControl>) -> Result<(), String> {
    if fc.frame_type() != mac::FrameType::MGMT {
        return Err(format!("invalid frame type {}", fc.frame_type().0));
    }

    if ht_ctrl.is_some() && !fc.htc_order() {
        return Err("ht_ctrl is present but +HTC bit is not set".to_string());
    } else if ht_ctrl.is_none() && fc.htc_order() {
        return Err("+HTC bit is set but ht_ctrl is missing".to_string());
    }

    if fc.to_ds() {
        return Err("to_ds must be set to 0 for non-QoS mgmt frames".to_string());
    }

    if fc.from_ds() {
        return Err("from_ds must be set to 0 for non-QoS mgmt frames".to_string());
    }

    Ok(())
}

pub fn write_mgmt_hdr<B: Appendable>(
    w: &mut B,
    fixed: MgmtHdr,
    ht_ctrl: Option<HtControl>,
) -> Result<(), FrameWriteError> {
    if let Err(message) = validate_frame_ctrl(fixed.frame_ctrl, ht_ctrl) {
        return Err(FrameWriteError::InvalidData {
            debug_message: format!("attempted to write an invalid mgmt frame header: {}", message),
        });
    }
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
        let got = mgmt_hdr_to_ap(FrameControl(1234), [1; 6], [2; 6], SequenceControl(4321));
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
                frame_ctrl: FrameControl(0b00110000_00110000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            None,
        );
        assert_eq!(result, Err(FrameWriteError::BufferTooSmall));
    }

    #[test]
    fn htc_set_but_ht_ctrl_is_missing() {
        let result = write_mgmt_hdr(
            &mut vec![],
            MgmtHdr {
                frame_ctrl: FrameControl(0b10110000_00110000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            None,
        );
        assert_invalid_data(result, "+HTC bit is set but ht_ctrl is missing");
    }

    #[test]
    fn ht_ctrl_present_but_no_htc() {
        let result = write_mgmt_hdr(
            &mut vec![],
            MgmtHdr {
                frame_ctrl: FrameControl(0b00110000_00110000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            Some(HtControl(0)),
        );
        assert_invalid_data(result, "ht_ctrl is present but +HTC bit is not set");
    }

    #[test]
    fn to_ds_set() {
        let result = write_mgmt_hdr(
            &mut vec![],
            MgmtHdr {
                frame_ctrl: FrameControl(0).with_to_ds(true),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            None,
        );
        assert_invalid_data(result, "to_ds must be set to 0");
    }

    #[test]
    fn from_ds_set() {
        let result = write_mgmt_hdr(
            &mut vec![],
            MgmtHdr {
                frame_ctrl: FrameControl(0).with_from_ds(true),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            None,
        );
        assert_invalid_data(result, "from_ds must be set to 0");
    }

    #[test]
    fn write_fixed_fields_only() {
        let mut bytes = vec![];
        write_mgmt_hdr(
            &mut bytes,
            MgmtHdr {
                frame_ctrl: FrameControl(0b00110000_00110000),
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
                0b00110000u8, 0b00110000, // Frame Control
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
                frame_ctrl: FrameControl(0b10110000_00110000),
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
                0b00110000u8, 0b10110000, // Frame Control
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

    fn assert_invalid_data(r: Result<(), FrameWriteError>, msg_part: &str) {
        match r {
            Err(FrameWriteError::InvalidData { debug_message }) => {
                if !debug_message.contains(msg_part) {
                    panic!(
                        "expected the error message `{}` to contain `{}` as a substring",
                        debug_message, msg_part
                    );
                }
            }
            other => panic!("expected Err(FrameWriteError::InvalidData), got {:?}", other),
        }
    }
}
