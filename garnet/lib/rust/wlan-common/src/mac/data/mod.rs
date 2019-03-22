// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::ByteSlice;

mod amsdu;
mod fields;
mod msdu;

pub use {amsdu::*, fields::*, msdu::*};

// IEEE Std 802.11-2016, 9.2.4.1.3
// Data subtypes:
pub const DATA_SUBTYPE_DATA: u16 = 0x00;
pub const DATA_SUBTYPE_NULL_DATA: u16 = 0x04;
pub const DATA_SUBTYPE_QOS_DATA: u16 = 0x08;
pub const DATA_SUBTYPE_NULL_QOS_DATA: u16 = 0x0C;

// IEEE Std 802.11-2016, 9.2.4.1.3, Table 9-1
pub const BITMASK_NULL: u16 = 1 << 2;
pub const BITMASK_QOS: u16 = 1 << 3;

pub enum DataFrameBody<B> {
    Llc { llc_frame: B },
    Amsdu { amsdu: B },
}

pub enum DataSubtype<B> {
    // QoS or regular data type.
    Data(DataFrameBody<B>),
    Unsupported { subtype: u16 },
}

impl<B: ByteSlice> DataSubtype<B> {
    pub fn parse(subtype: u16, qos_ctrl: Option<QosControl>, bytes: B) -> Option<DataSubtype<B>> {
        Some(match subtype {
            DATA_SUBTYPE_DATA => DataSubtype::Data(DataFrameBody::Llc { llc_frame: bytes }),
            DATA_SUBTYPE_QOS_DATA => {
                if qos_ctrl?.amsdu_present() {
                    DataSubtype::Data(DataFrameBody::Amsdu { amsdu: bytes })
                } else {
                    DataSubtype::Data(DataFrameBody::Llc { llc_frame: bytes })
                }
            }
            subtype => DataSubtype::Unsupported { subtype },
        })
    }
}

#[cfg(test)]
mod tests {
    use {crate::mac::*, crate::test_utils::fake_frames::*};

    #[test]
    fn parse_data_frame() {
        let bytes = make_data_frame_single_llc(None, None);
        match MacFrame::parse(&bytes[..], false) {
            Some(MacFrame::Data { fixed_fields, addr4, qos_ctrl, ht_ctrl, body }) => {
                assert_eq!(0b00000000_10001000, { fixed_fields.frame_ctrl.0 });
                assert_eq!(0x0202, { fixed_fields.duration });
                assert_eq!([3, 3, 3, 3, 3, 3], fixed_fields.addr1);
                assert_eq!([4, 4, 4, 4, 4, 4], fixed_fields.addr2);
                assert_eq!([5, 5, 5, 5, 5, 5], fixed_fields.addr3);
                assert_eq!(0x0606, { fixed_fields.seq_ctrl.0 });
                assert!(addr4.is_none());
                match qos_ctrl {
                    None => panic!("qos_ctrl expected to be present"),
                    Some(qos_ctrl) => {
                        assert_eq!(0x0101, qos_ctrl.get().0);
                    }
                };
                assert!(ht_ctrl.is_none());
                assert_eq!(&body[..], &[7, 7, 7, 8, 8, 8, 9, 10, 11, 11, 11]);
            }
            _ => panic!("failed parsing data frame"),
        };
    }

    #[test]
    fn parse_data_frame_with_padding() {
        let bytes = make_data_frame_with_padding();
        match MacFrame::parse(&bytes[..], true) {
            Some(MacFrame::Data { qos_ctrl, body, .. }) => {
                assert_eq!(0x0101, qos_ctrl.expect("qos_ctrl not present").get().0);
                assert_eq!(&[7, 7, 7, 8, 8, 8, 9, 10, 11, 11, 11, 11, 11], &body[..]);
            }
            _ => panic!("failed parsing data frame"),
        };
    }
}
