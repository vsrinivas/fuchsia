// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mac::{FrameControl, HtControl, MacAddr, OptionalField, Presence, SequenceControl},
    wlan_bitfield::bitfield,
    zerocopy::{AsBytes, FromBytes, Unaligned},
};

// IEEE Std 802.11-2016, 9.2.4.5.1, Table 9-6
#[bitfield(
    0..=3   tid,
    4       eosp,
    5..=6   ack_policy,
    7       amsdu_present,
    8..=15  high_byte, // interpretation varies
)]
#[repr(C)]
#[derive(AsBytes, FromBytes, Copy, Clone, PartialEq, Eq)]
pub struct QosControl(pub u16);

pub type Addr4 = MacAddr;

// IEEE Std 802.11-2016, 9.3.2.1
#[derive(FromBytes, AsBytes, Unaligned, PartialEq, Eq, Clone, Copy, Debug)]
#[repr(C, packed)]
pub struct FixedDataHdrFields {
    pub frame_ctrl: FrameControl,
    pub duration: u16,
    pub addr1: MacAddr,
    pub addr2: MacAddr,
    pub addr3: MacAddr,
    pub seq_ctrl: SequenceControl,
}

impl FixedDataHdrFields {
    /// Returns the length in bytes of a data header including all its fixed and optional
    /// fields (if they are present).
    pub fn len(
        has_addr4: Presence<Addr4>,
        has_qos_ctrl: Presence<QosControl>,
        has_ht_ctrl: Presence<HtControl>,
    ) -> usize {
        let mut bytes = std::mem::size_of::<Self>();
        bytes += match has_addr4 {
            Addr4::PRESENT => std::mem::size_of::<MacAddr>(),
            Addr4::ABSENT => 0,
        };
        bytes += match has_qos_ctrl {
            QosControl::PRESENT => std::mem::size_of::<QosControl>(),
            QosControl::ABSENT => 0,
        };
        bytes += match has_ht_ctrl {
            HtControl::PRESENT => std::mem::size_of::<HtControl>(),
            HtControl::ABSENT => 0,
        };
        bytes
    }
}

pub struct OptionalDataHdrFields {
    pub addr4: Option<Addr4>,
    pub qos_ctrl: Option<QosControl>,
    pub ht_ctrl: Option<HtControl>,
}
impl OptionalDataHdrFields {
    pub fn none() -> Self {
        Self { addr4: None, qos_ctrl: None, ht_ctrl: None }
    }
}

// IEEE Std 802.11-2016, Table 9-26 defines DA, SA, RA, TA, BSSID
pub fn data_dst_addr(hdr: &FixedDataHdrFields) -> MacAddr {
    let fc = hdr.frame_ctrl;
    if fc.to_ds() {
        hdr.addr3
    } else {
        hdr.addr1
    }
}

pub fn data_src_addr(hdr: &FixedDataHdrFields, addr4: Option<MacAddr>) -> Option<MacAddr> {
    let fc = hdr.frame_ctrl;
    match (fc.to_ds(), fc.from_ds()) {
        (_, false) => Some(hdr.addr2),
        (false, true) => Some(hdr.addr3),
        (true, true) => addr4,
    }
}

pub fn data_transmitter_addr(hdr: &FixedDataHdrFields) -> MacAddr {
    hdr.addr2
}

pub fn data_receiver_addr(hdr: &FixedDataHdrFields) -> MacAddr {
    hdr.addr1
}

/// BSSID: basic service set ID
pub fn data_bssid(hdr: &FixedDataHdrFields) -> Option<MacAddr> {
    let fc = hdr.frame_ctrl;
    match (fc.to_ds(), fc.from_ds()) {
        (false, false) => Some(hdr.addr3),
        (false, true) => Some(hdr.addr2),
        (true, false) => Some(hdr.addr1),
        (true, true) => None,
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::mac::*, crate::test_utils::fake_frames::*};

    #[test]
    fn fixed_fields_len() {
        assert_eq!(
            FixedDataHdrFields::len(Addr4::ABSENT, QosControl::ABSENT, HtControl::ABSENT),
            24
        );
        assert_eq!(
            FixedDataHdrFields::len(Addr4::PRESENT, QosControl::ABSENT, HtControl::ABSENT),
            30
        );
        assert_eq!(
            FixedDataHdrFields::len(Addr4::ABSENT, QosControl::PRESENT, HtControl::ABSENT),
            26
        );
        assert_eq!(
            FixedDataHdrFields::len(Addr4::ABSENT, QosControl::ABSENT, HtControl::PRESENT),
            28
        );
        assert_eq!(
            FixedDataHdrFields::len(Addr4::PRESENT, QosControl::PRESENT, HtControl::ABSENT),
            32
        );
        assert_eq!(
            FixedDataHdrFields::len(Addr4::ABSENT, QosControl::PRESENT, HtControl::PRESENT),
            30
        );
        assert_eq!(
            FixedDataHdrFields::len(Addr4::PRESENT, QosControl::ABSENT, HtControl::PRESENT),
            34
        );
        assert_eq!(
            FixedDataHdrFields::len(Addr4::PRESENT, QosControl::PRESENT, HtControl::PRESENT),
            36
        );
    }

    #[test]
    fn fixed_fields_dst_addr() {
        let mut fixed_fields = make_data_hdr(None, [0, 0], None);
        let (mut fixed_fields, _) =
            LayoutVerified::<_, FixedDataHdrFields>::new_unaligned_from_prefix(
                &mut fixed_fields[..],
            )
            .expect("invalid data header");
        let mut fc = FrameControl(0);
        fc.set_to_ds(true);
        fixed_fields.frame_ctrl = fc;
        assert_eq!(data_dst_addr(&fixed_fields), [5; 6]); // Addr3
        fc.set_to_ds(false);
        fixed_fields.frame_ctrl = fc;
        assert_eq!(data_dst_addr(&fixed_fields), [3; 6]); // Addr1
    }

    #[test]
    fn fixed_fields_src_addr() {
        let mut fixed_fields = make_data_hdr(None, [0, 0], None);
        let (mut fixed_fields, _) =
            LayoutVerified::<_, FixedDataHdrFields>::new_unaligned_from_prefix(
                &mut fixed_fields[..],
            )
            .expect("invalid data header");
        let mut fc = FrameControl(0);
        // to_ds == false && from_ds == false
        fixed_fields.frame_ctrl = fc;
        assert_eq!(data_src_addr(&fixed_fields, None), Some([4; 6])); // Addr2

        fc.set_to_ds(true);
        // to_ds == true && from_ds == false
        fixed_fields.frame_ctrl = fc;
        assert_eq!(data_src_addr(&fixed_fields, None), Some([4; 6])); // Addr2

        fc.set_from_ds(true);
        // to_ds == true && from_ds == true;
        fixed_fields.frame_ctrl = fc;
        assert_eq!(data_src_addr(&fixed_fields, Some([11; 6])), Some([11; 6])); // Addr4

        fc.set_to_ds(false);
        // to_ds == false && from_ds == true;
        fixed_fields.frame_ctrl = fc;
        assert_eq!(data_src_addr(&fixed_fields, None), Some([5; 6])); // Addr3
    }

    #[test]
    fn fixed_fields_ta() {
        let mut fixed_fields = make_data_hdr(None, [0, 0], None);
        let (fixed_fields, _) = LayoutVerified::<_, FixedDataHdrFields>::new_unaligned_from_prefix(
            &mut fixed_fields[..],
        )
        .expect("invalid data header");
        assert_eq!(data_transmitter_addr(&fixed_fields), [4; 6]); // Addr2
    }

    #[test]
    fn fixed_fields_ra() {
        let mut fixed_fields = make_data_hdr(None, [0, 0], None);
        let (fixed_fields, _) = LayoutVerified::<_, FixedDataHdrFields>::new_unaligned_from_prefix(
            &mut fixed_fields[..],
        )
        .expect("invalid data header");
        assert_eq!(data_receiver_addr(&fixed_fields), [3; 6]); // Addr2
    }

    #[test]
    fn fixed_fields_bssid() {
        let mut fixed_fields = make_data_hdr(None, [0, 0], None);
        let (mut fixed_fields, _) =
            LayoutVerified::<_, FixedDataHdrFields>::new_unaligned_from_prefix(
                &mut fixed_fields[..],
            )
            .expect("invalid data header");
        let mut fc = FrameControl(0);
        // to_ds == false && from_ds == false
        fixed_fields.frame_ctrl = fc;
        assert_eq!(data_bssid(&fixed_fields), Some([5; 6])); // Addr3

        fc.set_to_ds(true);
        // to_ds == true && from_ds == false
        fixed_fields.frame_ctrl = fc;
        assert_eq!(data_bssid(&fixed_fields), Some([3; 6])); // Addr1

        fc.set_from_ds(true);
        // to_ds == true && from_ds == true;
        fixed_fields.frame_ctrl = fc;
        assert_eq!(data_bssid(&fixed_fields), None);

        fc.set_to_ds(false);
        // to_ds == false && from_ds == true;
        fixed_fields.frame_ctrl = fc;
        assert_eq!(data_bssid(&fixed_fields), Some([4; 6])); // Addr2
    }
}
