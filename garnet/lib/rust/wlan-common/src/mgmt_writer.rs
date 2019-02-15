// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        buffer_writer::{BufferWriter, ByteSliceMut},
        mac::{self, FrameControl, HtControl, MgmtHdr, RawHtControl},
    },
    failure::{ensure, Error},
};

type MacAddr = [u8; 6];

#[derive(PartialEq)]
pub struct FixedFields {
    pub frame_ctrl: FrameControl,
    pub addr1: MacAddr,
    pub addr2: MacAddr,
    pub addr3: MacAddr,
    pub seq_ctrl: u16,
}
impl FixedFields {
    pub fn sent_from_client(
        mut frame_ctrl: FrameControl,
        bssid: MacAddr,
        client_addr: MacAddr,
        seq_ctrl: u16,
    ) -> FixedFields {
        FixedFields { frame_ctrl, addr1: bssid, addr2: client_addr, addr3: bssid, seq_ctrl }
    }
}
impl std::fmt::Debug for FixedFields {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        writeln!(
            f,
            "fc: {:#b}, addr1: {:02X?}, addr2: {:02X?}, addr3: {:02X?}, seq: {}",
            self.frame_ctrl.value(),
            self.addr1,
            self.addr2,
            self.addr3,
            self.seq_ctrl
        )?;
        Ok(())
    }
}

pub fn write_mgmt_hdr<B: ByteSliceMut>(
    w: BufferWriter<B>,
    mut fixed: FixedFields,
    ht_ctrl: Option<HtControl>,
) -> Result<BufferWriter<B>, Error> {
    fixed.frame_ctrl.set_frame_type(mac::FRAME_TYPE_MGMT);
    if ht_ctrl.is_some() {
        fixed.frame_ctrl.set_htc_order(true);
    } else {
        ensure!(!fixed.frame_ctrl.htc_order(), "htc_order bit set while HT-Control is absent");
    }

    let (mut mgmt_hdr, mut w) = w.reserve_zeroed::<MgmtHdr>()?;
    mgmt_hdr.set_frame_ctrl(fixed.frame_ctrl.value());
    mgmt_hdr.addr1 = fixed.addr1;
    mgmt_hdr.addr2 = fixed.addr2;
    mgmt_hdr.addr3 = fixed.addr3;
    mgmt_hdr.set_seq_ctrl(fixed.seq_ctrl);

    match ht_ctrl {
        None => Ok(w),
        Some(ht_ctrl_bitfield) => {
            let (mut ht_ctrl, mut w) = w.reserve_zeroed::<RawHtControl>()?;
            ht_ctrl.set(ht_ctrl_bitfield.value());
            Ok(w)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fixed_fields_sent_from_client() {
        let got = FixedFields::sent_from_client(FrameControl(1234), [1; 6], [2; 6], 4321);
        let expected = FixedFields {
            frame_ctrl: FrameControl(1234),
            addr1: [1; 6],
            addr2: [2; 6],
            addr3: [1; 6],
            seq_ctrl: 4321,
        };
        assert_eq!(got, expected);
    }

    #[test]
    fn too_small_buffer() {
        let mut bytes = vec![0u8; 20];
        let result = write_mgmt_hdr(
            BufferWriter::new(&mut bytes[..]),
            FixedFields {
                frame_ctrl: FrameControl(0b00110001_00110000),
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: 0b11000000_10010000,
            },
            None,
        );
        assert!(result.is_err(), "expected failure when writing into too small buffer");
    }

    #[test]
    fn invalid_ht_configuration() {
        let mut bytes = vec![0u8; 30];
        let result = write_mgmt_hdr(
            BufferWriter::new(&mut bytes[..]),
            FixedFields {
                frame_ctrl: FrameControl(0b10110001_00110000),
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: 0b11000000_10010000,
            },
            None,
        );
        assert!(result.is_err(), "expected failure due to invalid ht configuration");
    }

    #[test]
    fn write_fixed_fields_only() {
        let mut bytes = vec![0u8; 30];
        let w = write_mgmt_hdr(
            BufferWriter::new(&mut bytes[..]),
            FixedFields {
                frame_ctrl: FrameControl(0b00110001_00110000),
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: 0b11000000_10010000,
            },
            None,
        )
        .expect("Failed writing mgmt frame");

        assert_eq!(w.written_bytes(), 24);

        #[rustfmt::skip]
        assert_eq!(
            bytes,
            [
                // Data Header
                0b00110000u8, 0b00110001, // Frame Control
                0, 0, // duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                3, 3, 3, 3, 3, 3, // addr3
                0b10010000, 0b11000000, // Sequence Control
                // Trailing bytes
                0, 0, 0, 0, 0, 0,
            ]
        );
    }

    #[test]
    fn write_ht_ctrl() {
        let mut bytes = vec![0u8; 30];
        let w = write_mgmt_hdr(
            BufferWriter::new(&mut bytes[..]),
            FixedFields {
                frame_ctrl: FrameControl(0b00110001_00110000),
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: 0b11000000_10010000,
            },
            Some(HtControl(0b10101111_11000011_11110000_10101010)),
        )
        .expect("Failed writing mgmt frame");

        assert_eq!(w.written_bytes(), 28);

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
                // Trailing byte
                0, 0,
            ][..]
        );
    }
}
