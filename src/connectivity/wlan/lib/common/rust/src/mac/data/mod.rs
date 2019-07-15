// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod amsdu;
mod fields;
mod msdu;

pub use {amsdu::*, fields::*, msdu::*};

#[cfg(test)]
mod tests {
    use {
        crate::mac::*,
        crate::{assert_variant, test_utils::fake_frames::*},
    };

    #[test]
    fn parse_data_frame() {
        let bytes = make_data_frame_single_llc(None, None);
        assert_variant!(
            MacFrame::parse(&bytes[..], false),
            Some(MacFrame::Data { fixed_fields, addr4, qos_ctrl, ht_ctrl, body }) => {
                assert_eq!(0b00000000_10001000, { fixed_fields.frame_ctrl.0 });
                assert_eq!(0x0202, { fixed_fields.duration });
                assert_eq!([3, 3, 3, 3, 3, 3], fixed_fields.addr1);
                assert_eq!([4, 4, 4, 4, 4, 4], fixed_fields.addr2);
                assert_eq!([5, 5, 5, 5, 5, 5], fixed_fields.addr3);
                assert_eq!(0x0606, { fixed_fields.seq_ctrl.0 });
                assert!(addr4.is_none());
                assert_eq!(0x0101, qos_ctrl.expect("qos_ctrl not present").get().0);
                assert!(ht_ctrl.is_none());
                assert_eq!(&body[..], &[7, 7, 7, 8, 8, 8, 9, 10, 11, 11, 11]);
            },
            "expected data frame"
        );
    }

    #[test]
    fn parse_data_frame_with_padding() {
        let bytes = make_data_frame_with_padding();
        assert_variant!(
            MacFrame::parse(&bytes[..], true),
            Some(MacFrame::Data { qos_ctrl, body, .. }) => {
                assert_eq!(0x0101, qos_ctrl.expect("qos_ctrl not present").get().0);
                assert_eq!(&[7, 7, 7, 8, 8, 8, 9, 10, 11, 11, 11, 11, 11], &body[..]);
            },
            "expected data frame"
        );
    }
}
