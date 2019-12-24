// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        appendable::Appendable,
        big_endian::BigEndianU16,
        error::FrameWriteError,
        mac::{
            self, Bssid, FixedDataHdrFields, FrameControl, MacAddr, OptionalDataHdrFields,
            SequenceControl,
        },
    },
    anyhow::Error,
};

pub fn data_hdr_client_to_ap(
    mut frame_ctrl: FrameControl,
    bssid: Bssid,
    client_addr: MacAddr,
    seq_ctrl: SequenceControl,
) -> FixedDataHdrFields {
    frame_ctrl.set_to_ds(true);
    frame_ctrl.set_from_ds(false);
    FixedDataHdrFields {
        frame_ctrl,
        duration: 0,
        addr1: bssid.0,
        addr2: client_addr,
        addr3: bssid.0,
        seq_ctrl,
    }
}

fn validate_frame_ctrl(fc: FrameControl, optional: &OptionalDataHdrFields) -> Result<(), String> {
    if fc.frame_type() != mac::FrameType::DATA {
        return Err(format!("invalid frame type {}", fc.frame_type().0));
    }

    if optional.addr4.is_some() && !(fc.to_ds() && fc.from_ds()) {
        return Err(format!("addr4 is present but to_ds={}, from_ds={}", fc.to_ds(), fc.from_ds()));
    } else if optional.addr4.is_none() && fc.to_ds() && fc.from_ds() {
        return Err("to_ds and from_ds are both set but addr4 is missing".to_string());
    }

    if optional.qos_ctrl.is_some() && !fc.data_subtype().qos() {
        return Err("qos_ctrl is present but QoS bit is not set".to_string());
    } else if optional.qos_ctrl.is_none() && fc.data_subtype().qos() {
        return Err("QoS bit is set but qos_ctrl is missing".to_string());
    }

    if optional.ht_ctrl.is_some() && !fc.htc_order() {
        return Err("ht_ctrl is present but +HTC bit is not set".to_string());
    } else if optional.ht_ctrl.is_none() && fc.htc_order() {
        return Err("+HTC bit is set but ht_ctrl is missing".to_string());
    }

    Ok(())
}

pub fn write_data_hdr<B: Appendable>(
    w: &mut B,
    fixed: FixedDataHdrFields,
    optional: OptionalDataHdrFields,
) -> Result<(), FrameWriteError> {
    if let Err(message) = validate_frame_ctrl(fixed.frame_ctrl, &optional) {
        return Err(FrameWriteError::InvalidData {
            debug_message: format!("attempted to write an invalid data frame header: {}", message),
        });
    }

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
    w.append_value(&mac::LlcHdr {
        dsap: mac::LLC_SNAP_EXTENSION,
        ssap: mac::LLC_SNAP_EXTENSION,
        control: mac::LLC_SNAP_UNNUMBERED_INFO,
        oui: mac::LLC_SNAP_OUI,
        protocol_id: BigEndianU16::from_native(protocol_id),
    })?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            assert_variant, buffer_writer::BufferWriter, mac::FrameType, mac::HtControl,
            mac::QosControl,
        },
    };

    #[test]
    fn client_to_ap() {
        let got = data_hdr_client_to_ap(
            FrameControl(0b00110000_00110000),
            Bssid([1; 6]),
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
                frame_ctrl: FrameControl(0).with_frame_type(FrameType::DATA),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields::none(),
        );
        assert_eq!(result, Err(FrameWriteError::BufferTooSmall));
    }

    #[test]
    fn wrong_frame_type() {
        let result = write_data_hdr(
            &mut vec![],
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0).with_frame_type(FrameType::MGMT),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields::none(),
        );
        assert_invalid_data(result, "invalid frame type 0");
    }

    #[test]
    fn htc_set_but_ht_ctrl_missing() {
        let result = write_data_hdr(
            &mut vec![],
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0).with_frame_type(FrameType::DATA).with_htc_order(true),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields::none(),
        );
        assert_invalid_data(result, "+HTC bit is set but ht_ctrl is missing");
    }

    #[test]
    fn ht_ctrl_present_but_no_htc() {
        let result = write_data_hdr(
            &mut vec![],
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0).with_frame_type(FrameType::DATA),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields { addr4: None, qos_ctrl: None, ht_ctrl: Some(HtControl(0)) },
        );
        assert_invalid_data(result, "ht_ctrl is present but +HTC bit is not set");
    }

    #[test]
    fn to_from_ds_both_set_but_addr4_missing() {
        let result = write_data_hdr(
            &mut vec![],
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0)
                    .with_frame_type(FrameType::DATA)
                    .with_to_ds(true)
                    .with_from_ds(true),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields::none(),
        );
        assert_invalid_data(result, "to_ds and from_ds are both set but addr4 is missing");
    }

    #[test]
    fn addr4_is_present_but_to_from_are_invalid() {
        let result = write_data_hdr(
            &mut vec![],
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0).with_frame_type(FrameType::DATA).with_to_ds(true),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields { addr4: Some([4u8; 6]), qos_ctrl: None, ht_ctrl: None },
        );
        assert_invalid_data(result, "addr4 is present but to_ds=true, from_ds=false");
    }

    #[test]
    fn qos_set_but_qos_ctrl_missing() {
        let result = write_data_hdr(
            &mut vec![],
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0)
                    .with_frame_type(FrameType::DATA)
                    .with_frame_subtype(0x8),
                duration: 0,
                addr1: [1; 6],
                addr2: [2; 6],
                addr3: [3; 6],
                seq_ctrl: SequenceControl(0b11000000_10010000),
            },
            OptionalDataHdrFields::none(),
        );
        assert_invalid_data(result, "QoS bit is set but qos_ctrl is missing");
    }

    #[test]
    fn qos_ctrl_present_but_no_qos_bit() {
        let result = write_data_hdr(
            &mut vec![],
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0).with_frame_type(FrameType::DATA),
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
        );
        assert_invalid_data(result, "qos_ctrl is present but QoS bit is not set");
    }

    #[test]
    fn write_fixed_fields_only() {
        let mut bytes = vec![];
        write_data_hdr(
            &mut bytes,
            FixedDataHdrFields {
                frame_ctrl: FrameControl(0b00110001_0011_10_00),
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
                frame_ctrl: FrameControl(0b10110011_00111000),
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
                frame_ctrl: FrameControl(0b00110001_10111000),
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

    fn assert_invalid_data(r: Result<(), FrameWriteError>, msg_part: &str) {
        assert_variant!(r, Err(FrameWriteError::InvalidData { debug_message }) => {
            assert!(
                debug_message.contains(msg_part),
                "expected the error message `{}` to contain `{}` as a substring",
                debug_message, msg_part
            );
        });
    }
}
