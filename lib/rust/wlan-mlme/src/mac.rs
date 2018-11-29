// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bitfield::bitfield,
    byteorder::{ByteOrder, LittleEndian},
    zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned},
};

// This macro is used in combination with 802.11 definitions.
// These definitions have the same memory layout as their 802.11 silbing due to repr(C, packed)).
// Thus, we can simply reinterpret the bytes of these header as one of the corresponding structs,
// and can safely access its fields.
// Note the following caveats:
// - We cannot make any guarantees about the alignment of an instance of these
//   structs in memory or of any of its fields. This is true both because
//   repr(packed) removes the padding that would be used to ensure the alignment
//   of individual fields, but also because we are given no guarantees about
//   where within a given memory buffer a particular packet (and thus its
//   header) will be located.
// - Individual fields are all either u8 or [u8; N] rather than u16, u32, etc.
//   This is for two reasons:
//   - u16 and larger have larger-than-1 alignments, which are forbidden as
//     described above
//   - We are not guaranteed that the local platform has the same endianness as a header is defined
//     in, so simply treating a sequence of bytes
//     as a u16 or other multi-byte number would not necessarily be correct.
//     Instead, we use explicitly specify the endianess for its reader and writer methods
//     to correctly access these fields.
macro_rules! unsafe_impl_zerocopy_traits {
    ($type:ty) => {
        unsafe impl FromBytes for $type {}
        unsafe impl AsBytes for $type {}
        unsafe impl Unaligned for $type {}
    };
}

// IEEE Std 802.11-2016, 9.2.4.1.3
// Frame types:
const FRAME_TYPE_MGMT: u16 = 0;
// Management subtypes:
const MGMT_SUBTYPE_ASSOC_RESP: u16 = 0x01;
const MGMT_SUBTYPE_BEACON: u16 = 0x08;
const MGMT_SUBTYPE_AUTH: u16 = 0x0B;

// IEEE Std 802.11-2016, 9.2.4.1.1
bitfield! {
    #[derive(PartialEq)]
    pub struct FrameControl(u16);
    impl Debug;

    pub protocol_version, set_protocol_version: 1, 0;
    pub frame_type, set_frame_type: 3, 2;
    pub frame_subtype, set_frame_subtype: 7, 4;
    pub to_ds, set_to_ds: 8;
    pub from_ds, set_from_ds: 9;
    pub more_frag, set_more_frag: 19;
    pub retry, set_retry: 11;
    pub pwr_mgmt, set_pwr_mgmt: 12;
    pub more_data, set_more_data: 13;
    pub protected, set_protected: 14;
    pub htc_order, set_htc_order: 15;

    pub value, _: 15,0;
}

impl FrameControl {
    pub fn from_bytes(bytes: &[u8]) -> Option<FrameControl> {
        if bytes.len() < 2 {
            None
        } else {
            Some(FrameControl(LittleEndian::read_u16(bytes)))
        }
    }
}

// IEEE Std 802.11-2016, 9.2.4.6
bitfield! {
    #[derive(PartialEq)]
    pub struct HtControl(u32);
    impl Debug;

    pub vht, set_vht: 0;
    // Structure of this middle section is defined in 9.2.4.6.2 for HT,
    // and 9.2.4.6.3 for VHT.
    pub middle, set_middle: 29, 1;
    pub ac_constraint, set_ac_constraint: 30;
    pub rdg_more_ppdu, setrdg_more_ppdu: 31;

    pub value, _: 31,0;
}

impl HtControl {
    pub fn from_bytes(bytes: &[u8]) -> Option<HtControl> {
        if bytes.len() < 4 {
            None
        } else {
            Some(HtControl(LittleEndian::read_u32(bytes)))
        }
    }
}

// IEEE Std 802.11-2016, 9.3.3.2
#[repr(C, packed)]
pub struct MgmtHdr {
    pub frame_ctrl: [u8; 2],
    pub duration: [u8; 2],
    pub addr1: [u8; 6],
    pub addr2: [u8; 6],
    pub addr3: [u8; 6],
    pub seq_ctrl: [u8; 2],
}
// Safe: see macro explanation.
unsafe_impl_zerocopy_traits!(MgmtHdr);

impl MgmtHdr {
    pub fn frame_ctrl(&self) -> u16 {
        LittleEndian::read_u16(&self.frame_ctrl)
    }

    pub fn duration(&self) -> u16 {
        LittleEndian::read_u16(&self.duration)
    }

    pub fn seq_ctrl(&self) -> u16 {
        LittleEndian::read_u16(&self.seq_ctrl)
    }
}

pub enum MacFrame<B> {
    Mgmt {
        mgmt_hdr: LayoutVerified<B, MgmtHdr>,
        ht_ctrl: Option<LayoutVerified<B, [u8; 4]>>,
        body: B,
    },
    Unsupported {
        type_: u16,
    },
}

fn skip<B: ByteSlice>(bytes: B, skip: usize) -> Option<B> {
    if bytes.len() < skip {
        None
    } else {
        let (_, suffix) = bytes.split_at(skip);
        Some(suffix)
    }
}

impl<B: ByteSlice> MacFrame<B> {
    /// Padding is optional and follows after the MAC header.
    pub fn parse(bytes: B, padding: usize) -> Option<MacFrame<B>> {
        let fc = FrameControl::from_bytes(&bytes[..])?;
        match fc.frame_type() {
            FRAME_TYPE_MGMT => {
                let (mgmt_hdr, body) = LayoutVerified::new_from_prefix(bytes)?;
                let body = skip(body, padding)?;

                // HT Control field is optional.
                let (ht_ctrl, body) = if fc.htc_order() {
                    let (ht_ctrl, body) = LayoutVerified::new_from_prefix(body)?;
                    (Some(ht_ctrl), body)
                } else {
                    (None, body)
                };

                Some(MacFrame::Mgmt {
                    mgmt_hdr,
                    ht_ctrl,
                    body,
                })
            }
            type_ => Some(MacFrame::Unsupported { type_ }),
        }
    }
}

// IEEE Std 802.11-2016, 9.3.3.3
#[repr(C, packed)]
pub struct BeaconHdr {
    pub timestamp: [u8; 8],
    pub beacon_interval: [u8; 2],
    pub capabilities: [u8; 2],
}
// Safe: see macro explanation.
unsafe_impl_zerocopy_traits!(BeaconHdr);

impl BeaconHdr {
    pub fn timestamp(&self) -> u64 {
        LittleEndian::read_u64(&self.timestamp)
    }

    pub fn beacon_interval(&self) -> u16 {
        LittleEndian::read_u16(&self.beacon_interval)
    }

    pub fn capabilities(&self) -> u16 {
        LittleEndian::read_u16(&self.capabilities)
    }
}

// IEEE Std 802.11-2016, 9.3.3.12
#[repr(C, packed)]
pub struct AuthHdr {
    pub auth_alg_num: [u8; 2],
    pub auth_txn_seq_num: [u8; 2],
    pub status_code: [u8; 2],
}
// Safe: see macro explanation.
unsafe_impl_zerocopy_traits!(AuthHdr);

impl AuthHdr {
    pub fn auth_alg_num(&self) -> u16 {
        LittleEndian::read_u16(&self.auth_alg_num)
    }

    pub fn auth_txn_seq_num(&self) -> u16 {
        LittleEndian::read_u16(&self.auth_txn_seq_num)
    }

    pub fn status_code(&self) -> u16 {
        LittleEndian::read_u16(&self.status_code)
    }
}

// IEEE Std 802.11-2016, 9.3.3.6
#[repr(C, packed)]
pub struct AssocRespHdr {
    pub capabilities: [u8; 2],
    pub status_code: [u8; 2],
    pub aid: [u8; 2],
}
// Safe: see macro explanation.
unsafe_impl_zerocopy_traits!(AssocRespHdr);

impl AssocRespHdr {
    pub fn capabilities(&self) -> u16 {
        LittleEndian::read_u16(&self.capabilities)
    }

    pub fn status_code(&self) -> u16 {
        LittleEndian::read_u16(&self.status_code)
    }

    pub fn aid(&self) -> u16 {
        LittleEndian::read_u16(&self.aid)
    }
}

pub enum MgmtSubtype<B> {
    Beacon {
        hdr: LayoutVerified<B, BeaconHdr>,
        elements: B,
    },
    Authentication {
        hdr: LayoutVerified<B, AuthHdr>,
        elements: B,
    },
    AssociationResp {
        hdr: LayoutVerified<B, AssocRespHdr>,
        elements: B,
    },
    Unsupported {
        subtype: u16,
    },
}

impl<B: ByteSlice> MgmtSubtype<B> {
    pub fn parse(subtype: u16, bytes: B) -> Option<MgmtSubtype<B>> {
        match subtype {
            MGMT_SUBTYPE_BEACON => {
                let (hdr, elements) = LayoutVerified::new_from_prefix(bytes)?;
                Some(MgmtSubtype::Beacon { hdr, elements })
            }
            MGMT_SUBTYPE_AUTH => {
                let (hdr, elements) = LayoutVerified::new_from_prefix(bytes)?;
                Some(MgmtSubtype::Authentication { hdr, elements })
            }
            MGMT_SUBTYPE_ASSOC_RESP => {
                let (hdr, elements) = LayoutVerified::new_from_prefix(bytes)?;
                Some(MgmtSubtype::AssociationResp { hdr, elements })
            }
            subtype => Some(MgmtSubtype::Unsupported { subtype }),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_mgmt_frame(ht_ctrl: bool, padding: bool) -> Vec<u8> {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let mut bytes = vec![
            1, if ht_ctrl { 128 } else { 1 }, // fc
            2, 2, // duration
            3, 3, 3, 3, 3, 3, // addr1
            4, 4, 4, 4, 4, 4, // addr2
            5, 5, 5, 5, 5, 5, // addr3
            6, 6, // sequence control
        ];
        if padding {
            bytes.extend_from_slice(&[7, 7, 7, 7]);
        }
        if ht_ctrl {
            bytes.extend_from_slice(&[8, 8, 8, 8]);
        }
        bytes.extend_from_slice(&[9, 9, 9]);
        bytes
    }

    #[test]
    fn parse_mgmt_frame() {
        let bytes = make_mgmt_frame(false, false);
        match MacFrame::parse(&bytes[..], 0) {
            Some(MacFrame::Mgmt {
                mgmt_hdr,
                ht_ctrl,
                body,
            }) => {
                assert_eq!([1, 1], mgmt_hdr.frame_ctrl);
                assert_eq!([2, 2], mgmt_hdr.duration);
                assert_eq!([3, 3, 3, 3, 3, 3], mgmt_hdr.addr1);
                assert_eq!([4, 4, 4, 4, 4, 4], mgmt_hdr.addr2);
                assert_eq!([5, 5, 5, 5, 5, 5], mgmt_hdr.addr3);
                assert_eq!([6, 6], mgmt_hdr.seq_ctrl);
                assert!(ht_ctrl.is_none());
                assert_eq!(&body[..], &[9, 9, 9]);
            }
            _ => panic!("failed parsing mgmt frame"),
        };
    }

    #[test]
    fn parse_mgmt_frame_with_ht_ctrl_padding() {
        let bytes = make_mgmt_frame(true, true);
        match MacFrame::parse(&bytes[..], 4) {
            Some(MacFrame::Mgmt {
                mgmt_hdr,
                ht_ctrl,
                body,
            }) => {
                assert_eq!([6, 6], mgmt_hdr.seq_ctrl);
                assert_eq!([8, 8, 8, 8], *ht_ctrl.expect("ht_ctrl not present"));
                assert_eq!(&[9, 9, 9], &body[..]);
            }
            _ => panic!("failed parsing mgmt frame"),
        };
    }

    #[test]
    fn parse_mgmt_frame_too_short_unsupported() {
        // Valid MGMT header must have a minium length of 24 bytes.
        assert!(MacFrame::parse(&[0; 22][..], 0).is_none());

        // Unsupported frame type.
        match MacFrame::parse(&[0xFF; 24][..], 0) {
            Some(MacFrame::Unsupported { type_ }) => assert_eq!(3, type_),
            _ => panic!("didn't detect unsupported frame"),
        };
    }

    #[test]
    fn parse_beacon_frame() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let bytes = vec![
            1,1,1,1,1,1,1,1, // timestamp
            2,2, // beacon_interval
            3,3, // capabilities
            0,5,1,2,3,4,5 // SSID IE: "12345"
        ];
        match MgmtSubtype::parse(MGMT_SUBTYPE_BEACON, &bytes[..]) {
            Some(MgmtSubtype::Beacon { hdr, elements }) => {
                assert_eq!(0x0101010101010101, hdr.timestamp());
                assert_eq!(0x0202, hdr.beacon_interval());
                assert_eq!(0x0303, hdr.capabilities());
                assert_eq!(&[0, 5, 1, 2, 3, 4, 5], &elements[..]);
            }
            _ => panic!("failed parsing beacon frame"),
        };
    }

}
