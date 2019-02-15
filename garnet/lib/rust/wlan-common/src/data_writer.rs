// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        buffer_writer::{BufferWriter, ByteSliceMut, LayoutVerified},
        mac::{
            self, Addr4, DataHdr, FrameControl, HtControl, QosControl, RawHtControl, RawQosControl,
        },
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
        frame_ctrl.set_to_ds(true);
        FixedFields { frame_ctrl, addr1: bssid.clone(), addr2: client_addr, addr3: bssid, seq_ctrl }
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

pub struct OptionalFields {
    pub addr4: Option<Addr4>,
    pub qos_ctrl: Option<QosControl>,
    pub ht_ctrl: Option<HtControl>,
}
impl OptionalFields {
    pub fn none() -> OptionalFields {
        OptionalFields { addr4: None, qos_ctrl: None, ht_ctrl: None }
    }
}

pub fn write_data_hdr<B: ByteSliceMut>(
    w: BufferWriter<B>,
    mut fixed: FixedFields,
    optional: OptionalFields,
) -> Result<BufferWriter<B>, Error> {
    fixed.frame_ctrl.set_frame_type(mac::FRAME_TYPE_DATA);
    if optional.addr4.is_some() {
        fixed.frame_ctrl.set_from_ds(true);
        fixed.frame_ctrl.set_to_ds(true);
    } else {
        ensure!(
            fixed.frame_ctrl.from_ds() != fixed.frame_ctrl.to_ds(),
            "addr4 is absent but to- and from-ds bit are present"
        );
    }
    if optional.qos_ctrl.is_some() {
        fixed.frame_ctrl.set_frame_subtype(fixed.frame_ctrl.frame_subtype() | mac::BITMASK_QOS);
    } else {
        ensure!(
            fixed.frame_ctrl.frame_subtype() & mac::BITMASK_QOS == 0,
            "QoS bit set while QoS-Control is absent"
        );
    }
    if optional.ht_ctrl.is_some() {
        fixed.frame_ctrl.set_htc_order(true);
    } else {
        ensure!(!fixed.frame_ctrl.htc_order(), "htc_order bit set while HT-Control is absent");
    }

    let (mut data_hdr, mut w) = w.reserve_zeroed::<DataHdr>()?;
    data_hdr.set_frame_ctrl(fixed.frame_ctrl.value());
    data_hdr.addr1 = fixed.addr1;
    data_hdr.addr2 = fixed.addr2;
    data_hdr.addr3 = fixed.addr3;
    data_hdr.set_seq_ctrl(fixed.seq_ctrl);

    let mut w = match optional.addr4 {
        None => w,
        Some(addr4_value) => {
            let (mut addr4, mut w) = w.reserve_zeroed::<Addr4>()?;
            *addr4 = addr4_value;
            w
        }
    };

    let mut w = match optional.qos_ctrl {
        None => w,
        Some(qos_ctrl_bitfield) => {
            let (mut qos_ctrl, mut w) = w.reserve_zeroed::<RawQosControl>()?;
            qos_ctrl.set(qos_ctrl_bitfield.value());
            w
        }
    };

    let mut w = match optional.ht_ctrl {
        None => w,
        Some(ht_ctrl_bitfield) => {
            let (mut ht_ctrl, mut w) = w.reserve_zeroed::<RawHtControl>()?;
            ht_ctrl.set(ht_ctrl_bitfield.value());
            w
        }
    };

    Ok(w)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fixed_fields_sent_from_client() {
        let got =
            FixedFields::sent_from_client(FrameControl(0b00110000_00110000), [1; 6], [2; 6], 4321);
        let expected = FixedFields {
            frame_ctrl: FrameControl(0b00110001_00110000),
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
        let result = write_data_hdr(
            BufferWriter::new(&mut bytes[..]),
            FixedFields {
                frame_ctrl: FrameControl(0b00110001_00110000),
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: 0b11000000_10010000,
            },
            OptionalFields::none(),
        );
        assert!(result.is_err(), "expected failure when writing into too small buffer");
    }

    #[test]
    fn invalid_ht_configuration() {
        let mut bytes = vec![0u8; 30];
        let result = write_data_hdr(
            BufferWriter::new(&mut bytes[..]),
            FixedFields {
                frame_ctrl: FrameControl(0b10110001_00110000),
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: 0b11000000_10010000,
            },
            OptionalFields::none(),
        );
        assert!(result.is_err(), "expected failure due to invalid ht configuration");
    }

    #[test]
    fn invalid_addr4_configuration() {
        let mut bytes = vec![0u8; 30];
        let result = write_data_hdr(
            BufferWriter::new(&mut bytes[..]),
            FixedFields {
                frame_ctrl: FrameControl(0b00110011_00110000),
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: 0b11000000_10010000,
            },
            OptionalFields::none(),
        );
        assert!(result.is_err(), "expected failure due to invalid addr4 configuration");
    }

    #[test]
    fn invalid_qos_configuration() {
        let mut bytes = vec![0u8; 30];
        let result = write_data_hdr(
            BufferWriter::new(&mut bytes[..]),
            FixedFields {
                frame_ctrl: FrameControl(0b00110000_10110000),
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: 0b11000000_10010000,
            },
            OptionalFields::none(),
        );
        assert!(result.is_err(), "expected failure due to invalid qos configuration");
    }

    #[test]
    fn write_fixed_fields_only() {
        let mut bytes = vec![0u8; 30];
        let w = write_data_hdr(
            BufferWriter::new(&mut bytes[..]),
            FixedFields {
                frame_ctrl: FrameControl(0b00110001_0011_00_00),
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: 0b11000000_10010000,
            },
            OptionalFields::none(),
        )
        .expect("Failed writing data frame");

        assert_eq!(w.written_bytes(), 24);

        #[rustfmt::skip]
        assert_eq!(
            bytes,
            [
                // Data Header
                0b0011_10_00u8, 0b00110001, // Frame Control
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
    fn write_addr4_ht_ctrl() {
        let mut bytes = vec![0u8; 35];
        let w = write_data_hdr(
            BufferWriter::new(&mut bytes[..]),
            FixedFields {
                frame_ctrl: FrameControl(0b00110001_00111000),
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: 0b11000000_10010000,
            },
            OptionalFields {
                addr4: Some([4u8; 6]),
                qos_ctrl: None,
                ht_ctrl: Some(HtControl(0b10101111_11000011_11110000_10101010)),
            },
        )
        .expect("Failed writing data frame");

        assert_eq!(w.written_bytes(), 34);

        #[rustfmt::skip]
        assert_eq!(
            &bytes[..],
            &[
                // Data Header
                0b00111000u8, 0b10110011, // Frame Control
                0, 0, // duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                3, 3, 3, 3, 3, 3, // addr3
                0b10010000, 0b11000000, // Sequence Control
                // Addr4
                4, 4, 4, 4, 4, 4,
                // Ht Control
                0b10101010, 0b11110000, 0b11000011, 0b10101111,
                // Trailing byte
                0,
            ][..]
        );
    }

    #[test]
    fn write_qos_ctrl() {
        let mut bytes = vec![0u8; 30];
        let w = write_data_hdr(
            BufferWriter::new(&mut bytes[..]),
            FixedFields {
                frame_ctrl: FrameControl(0b00110001_00111000),
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: 0b11000000_10010000,
            },
            OptionalFields {
                addr4: None,
                qos_ctrl: Some(QosControl(0b11110000_10101010)),
                ht_ctrl: None,
            },
        )
        .expect("Failed writing data frame");

        assert_eq!(w.written_bytes(), 26);

        #[rustfmt::skip]
        assert_eq!(
            &bytes[..],
            &[
                // Data Header
                0b10111000u8, 0b00110001, // Frame Control
                0, 0, // duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                3, 3, 3, 3, 3, 3, // addr3
                0b10010000, 0b11000000, // Sequence Control
                // Qos Control
                0b10101010, 0b11110000,
                // Trailing bytes
                0, 0, 0, 0,
            ][..]
        );
    }
}
