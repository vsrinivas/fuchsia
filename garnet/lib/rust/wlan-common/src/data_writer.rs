// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        appendable::Appendable,
        mac::{self, FixedDataHdrFields, FrameControl, OptionalDataHdrFields, SequenceControl},
    },
    failure::{ensure, Error},
};

type MacAddr = [u8; 6];

pub fn data_hdr_client_to_ap(
    mut frame_ctrl: FrameControl,
    bssid: MacAddr,
    client_addr: MacAddr,
    seq_ctrl: SequenceControl,
) -> FixedDataHdrFields {
    frame_ctrl.set_to_ds(true);
    frame_ctrl.set_from_ds(false);
    FixedDataHdrFields {
        frame_ctrl,
        duration: 0,
        addr1: bssid.clone(),
        addr2: client_addr,
        addr3: bssid,
        seq_ctrl,
    }
}

fn make_new_frame_ctrl(
    mut fc: FrameControl,
    optional: &OptionalDataHdrFields,
) -> Result<FrameControl, Error> {
    fc.set_frame_type(mac::FRAME_TYPE_DATA);
    if optional.addr4.is_some() {
        fc.set_from_ds(true);
        fc.set_to_ds(true);
    } else {
        ensure!(
            !(fc.from_ds() && fc.to_ds()),
            "addr4 is absent but to- and from-ds bit are present"
        );
    }
    if optional.qos_ctrl.is_some() {
        fc.set_frame_subtype(fc.frame_subtype() | mac::BITMASK_QOS);
    } else {
        ensure!(
            fc.frame_subtype() & mac::BITMASK_QOS == 0,
            "QoS bit set while QoS-Control is absent"
        );
    }
    if optional.ht_ctrl.is_some() {
        fc.set_htc_order(true);
    } else {
        ensure!(!fc.htc_order(), "htc_order bit set while HT-Control is absent");
    }
    Ok(fc)
}

pub fn write_data_hdr<B: Appendable>(
    w: &mut B,
    mut fixed: FixedDataHdrFields,
    optional: OptionalDataHdrFields,
) -> Result<(), Error> {
    fixed.frame_ctrl = make_new_frame_ctrl(fixed.frame_ctrl, &optional)?;
    w.append_value(&fixed)?;
    if let Some(addr4) = optional.addr4.as_ref() {
        w.append_value(addr4)?;
    }
    if let Some(qos_ctrl) = optional.qos_ctrl.as_ref() {
        w.append_value(qos_ctrl)?;
    }
    if let Some(ht_ctrl) = optional.ht_ctrl.as_ref() {
        w.append_value(ht_ctrl)?;
    }
    Ok(())
}

pub fn write_snap_llc_hdr<B: Appendable>(w: &mut B, protocol_id: u16) -> Result<(), Error> {
    let mut llc_hdr = w.append_value_zeroed::<mac::LlcHdr>()?;
    llc_hdr.dsap = mac::LLC_SNAP_EXTENSION;
    llc_hdr.ssap = mac::LLC_SNAP_EXTENSION;
    llc_hdr.control = mac::LLC_SNAP_UNNUMBERED_INFO;
    llc_hdr.oui = mac::LLC_SNAP_OUI;
    llc_hdr.set_protocol_id(protocol_id);
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{buffer_writer::BufferWriter, mac::HtControl, mac::QosControl},
    };

    #[test]
    fn client_to_ap() {
        let got = data_hdr_client_to_ap(
            FrameControl(0b00110000_00110000),
            [1; 6],
            [2; 6],
            SequenceControl(4321),
        );
        let expected = FixedDataHdrFields {
            frame_ctrl: FrameControl(0b00110001_00110000),
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
        let result = write_data_hdr(
            &mut BufferWriter::new(&mut bytes[..]),
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0b00110001_00110000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields::none(),
        );
        assert!(result.is_err(), "expected failure when writing into too small buffer");
    }

    #[test]
    fn invalid_ht_configuration() {
        let result = write_data_hdr(
            &mut vec![],
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0b10110001_00110000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields::none(),
        );
        assert!(result.is_err(), "expected failure due to invalid ht configuration");
    }

    #[test]
    fn invalid_addr4_configuration() {
        let result = write_data_hdr(
            &mut vec![],
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0b00110011_00110000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields::none(),
        );
        assert!(result.is_err(), "expected failure due to invalid addr4 configuration");
    }

    #[test]
    fn invalid_qos_configuration() {
        let result = write_data_hdr(
            &mut vec![],
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0b00110000_10110000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields::none(),
        );
        assert!(result.is_err(), "expected failure due to invalid qos configuration");
    }

    #[test]
    fn write_fixed_fields_only() {
        let mut bytes = vec![];
        write_data_hdr(
            &mut bytes,
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0b00110001_0011_00_00),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields::none(),
        )
        .expect("Failed writing data frame");

        #[rustfmt::skip]
        assert_eq!(
            &bytes[..],
            &[
                // Data Header
                0b0011_10_00u8, 0b00110001, // Frame Control
                0, 0, // duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                3, 3, 3, 3, 3, 3, // addr3
                0b10010000, 0b11000000, // Sequence Control
            ]
        );
    }

    #[test]
    fn write_addr4_ht_ctrl() {
        let mut bytes = vec![];
        write_data_hdr(
            &mut bytes,
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0b00110001_00111000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields {
                addr4: Some([4u8; 6]),
                qos_ctrl: None,
                ht_ctrl: Some(HtControl(0b10101111_11000011_11110000_10101010)),
            },
        )
        .expect("Failed writing data frame");

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
            ][..]
        );
    }

    #[test]
    fn write_qos_ctrl() {
        let mut bytes = vec![];
        write_data_hdr(
            &mut bytes,
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0b00110001_00111000),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields {
                addr4: None,
                qos_ctrl: Some(QosControl(0b11110000_10101010)),
                ht_ctrl: None,
            },
        )
        .expect("Failed writing data frame");

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
            ][..]
        );
    }

    #[test]
    fn write_llc_hdr() {
        let mut bytes = vec![];
        write_snap_llc_hdr(&mut bytes, 0x888E).expect("Failed writing LLC header");

        #[rustfmt::skip]
        assert_eq!(
            &bytes[..],
            &[
                0xAA, 0xAA, 0x03, // DSAP, SSAP, Control
                0, 0, 0, // OUI
                0x88, 0x8E, // Protocol ID
            ]
        );
    }
}
