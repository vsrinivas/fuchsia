// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{ByteSlice, LayoutVerified};

mod fields;
mod reason;
mod status;

pub use {fields::*, reason::*, status::*};

// IEEE Std 802.11-2016, 9.2.4.1.3
// Management subtypes:
pub const MGMT_SUBTYPE_ASSOC_RESP: u16 = 0x01;
pub const MGMT_SUBTYPE_BEACON: u16 = 0x08;
pub const MGMT_SUBTYPE_AUTH: u16 = 0x0B;
pub const MGMT_SUBTYPE_DEAUTH: u16 = 0x0C;

pub enum MgmtSubtype<B> {
    Beacon { bcn_hdr: LayoutVerified<B, BeaconHdr>, elements: B },
    Authentication { auth_hdr: LayoutVerified<B, AuthHdr>, elements: B },
    AssociationResp { assoc_resp_hdr: LayoutVerified<B, AssocRespHdr>, elements: B },
    Unsupported { subtype: u16 },
}

impl<B: ByteSlice> MgmtSubtype<B> {
    pub fn parse(subtype: u16, bytes: B) -> Option<MgmtSubtype<B>> {
        match subtype {
            MGMT_SUBTYPE_BEACON => {
                let (bcn_hdr, elements) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
                Some(MgmtSubtype::Beacon { bcn_hdr, elements })
            }
            MGMT_SUBTYPE_AUTH => {
                let (auth_hdr, elements) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
                Some(MgmtSubtype::Authentication { auth_hdr, elements })
            }
            MGMT_SUBTYPE_ASSOC_RESP => {
                let (assoc_resp_hdr, elements) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
                Some(MgmtSubtype::AssociationResp { assoc_resp_hdr, elements })
            }
            subtype => Some(MgmtSubtype::Unsupported { subtype }),
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::mac::*};

    #[test]
    fn mgmt_hdr_len() {
        assert_eq!(MgmtHdr::len(HtControl::ABSENT), 24);
        assert_eq!(MgmtHdr::len(HtControl::PRESENT), 28);
    }

    #[test]
    fn parse_beacon_frame() {
        #[rustfmt::skip]
            let bytes = vec![
            1,1,1,1,1,1,1,1, // timestamp
            2,2, // beacon_interval
            3,3, // capabilities
            0,5,1,2,3,4,5 // SSID IE: "12345"
        ];
        match MgmtSubtype::parse(MGMT_SUBTYPE_BEACON, &bytes[..]) {
            Some(MgmtSubtype::Beacon { bcn_hdr, elements }) => {
                assert_eq!(0x0101010101010101, { bcn_hdr.timestamp });
                assert_eq!(0x0202, { bcn_hdr.beacon_interval });
                assert_eq!(0x0303, { bcn_hdr.capabilities.0 });
                assert_eq!(&[0, 5, 1, 2, 3, 4, 5], &elements[..]);
            }
            _ => panic!("failed parsing beacon frame"),
        };
    }
}
