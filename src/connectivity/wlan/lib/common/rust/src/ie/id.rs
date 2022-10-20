// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{AsBytes, FromBytes, Unaligned};

#[repr(C, packed)]
#[derive(Eq, PartialEq, Hash, AsBytes, FromBytes, Unaligned, Copy, Clone, Debug)]
pub struct Id(pub u8);

// IEEE Std 802.11-2016, 9.4.2.1, Table 9-77
impl Id {
    pub const SSID: Self = Self(0);
    pub const SUPPORTED_RATES: Self = Self(1);
    pub const DSSS_PARAM_SET: Self = Self(3);
    pub const TIM: Self = Self(5);
    pub const COUNTRY: Self = Self(7);
    pub const CHANNEL_SWITCH_ANNOUNCEMENT: Self = Self(37);
    pub const HT_CAPABILITIES: Self = Self(45);
    pub const RSNE: Self = Self(48);
    pub const EXTENDED_SUPPORTED_RATES: Self = Self(50);
    pub const MOBILITY_DOMAIN: Self = Self(54);
    pub const EXTENDED_CHANNEL_SWITCH_ANNOUNCEMENT: Self = Self(60);
    pub const HT_OPERATION: Self = Self(61);
    pub const SECONDARY_CHANNEL_OFFSET: Self = Self(62);
    pub const RM_ENABLED_CAPABILITIES: Self = Self(70);
    pub const BSS_MAX_IDLE_PERIOD: Self = Self(90);
    pub const MESH_PEERING_MGMT: Self = Self(117);
    pub const EXT_CAPABILITIES: Self = Self(127);
    pub const PREQ: Self = Self(130);
    pub const PREP: Self = Self(131);
    pub const PERR: Self = Self(132);
    pub const VHT_CAPABILITIES: Self = Self(191);
    pub const VHT_OPERATION: Self = Self(192);
    pub const WIDE_BANDWIDTH_CHANNEL_SWITCH: Self = Self(194);
    pub const TRANSMIT_POWER_ENVELOPE: Self = Self(195);
    pub const CHANNEL_SWITCH_WRAPPER: Self = Self(196);
    pub const VENDOR_SPECIFIC: Self = Self(221);
    pub const EXTENSION: Self = Self(255);
}

#[derive(Eq, PartialEq, Hash, Copy, Clone, Debug)]
pub enum IeType {
    Ieee {
        id: Id,
        extension: Option<u8>,
    },
    Vendor {
        // TODO(fxbug.dev/71573): The Vendor Specific element defined by IEEE 802.11-2016 9.4.2.26
        // does not have a header length of 6. Instead, the OUI is noted to be either 3 or 5
        // bytes and the vendor determines the remainder of the contents.
        vendor_ie_hdr: [u8; 6], // OUI, OUI type, version
    },
}

macro_rules! ie_type_basic_const {
    ($id:ident) => {
        pub const $id: Self = Self::new_basic(Id::$id);
    };
}

impl IeType {
    ie_type_basic_const!(SSID);
    ie_type_basic_const!(SUPPORTED_RATES);
    ie_type_basic_const!(DSSS_PARAM_SET);
    ie_type_basic_const!(TIM);
    ie_type_basic_const!(COUNTRY);
    ie_type_basic_const!(CHANNEL_SWITCH_ANNOUNCEMENT);
    ie_type_basic_const!(HT_CAPABILITIES);
    ie_type_basic_const!(RSNE);
    ie_type_basic_const!(EXTENDED_SUPPORTED_RATES);
    ie_type_basic_const!(MOBILITY_DOMAIN);
    ie_type_basic_const!(EXTENDED_CHANNEL_SWITCH_ANNOUNCEMENT);
    ie_type_basic_const!(HT_OPERATION);
    ie_type_basic_const!(SECONDARY_CHANNEL_OFFSET);
    ie_type_basic_const!(RM_ENABLED_CAPABILITIES);
    ie_type_basic_const!(BSS_MAX_IDLE_PERIOD);
    ie_type_basic_const!(MESH_PEERING_MGMT);
    ie_type_basic_const!(EXT_CAPABILITIES);
    ie_type_basic_const!(PREQ);
    ie_type_basic_const!(PREP);
    ie_type_basic_const!(PERR);
    ie_type_basic_const!(VHT_CAPABILITIES);
    ie_type_basic_const!(VHT_OPERATION);
    ie_type_basic_const!(WIDE_BANDWIDTH_CHANNEL_SWITCH);
    ie_type_basic_const!(TRANSMIT_POWER_ENVELOPE);
    ie_type_basic_const!(CHANNEL_SWITCH_WRAPPER);

    pub const WMM_INFO: Self = Self::new_vendor([0x00, 0x50, 0xf2, 0x02, 0x00, 0x01]);
    pub const WMM_PARAM: Self = Self::new_vendor([0x00, 0x50, 0xf2, 0x02, 0x01, 0x01]);

    pub const fn new_basic(id: Id) -> Self {
        Self::Ieee { id, extension: None }
    }

    pub const fn new_extended(ext_id: u8) -> Self {
        Self::Ieee { id: Id::EXTENSION, extension: Some(ext_id) }
    }

    pub const fn new_vendor(vendor_ie_hdr: [u8; 6]) -> Self {
        Self::Vendor { vendor_ie_hdr }
    }

    pub const fn basic_id(&self) -> Id {
        match self {
            Self::Ieee { id, .. } => *id,
            Self::Vendor { .. } => Id::VENDOR_SPECIFIC,
        }
    }

    /// Number of bytes consumed from the IE body to construct IeType
    pub fn extra_len(&self) -> usize {
        self.extra_bytes().len()
    }

    /// Return the bytes consumed from the IE body (not IE header) to construct IeType
    pub fn extra_bytes(&self) -> &[u8] {
        match self {
            Self::Ieee { extension, .. } => {
                extension.as_ref().map(|ext_id| std::slice::from_ref(ext_id)).unwrap_or(&[])
            }
            Self::Vendor { vendor_ie_hdr } => &vendor_ie_hdr[..],
        }
    }
}

impl std::cmp::PartialOrd for IeType {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl std::cmp::Ord for IeType {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        match (self, other) {
            (
                Self::Ieee { extension: Some(ext_id), .. },
                Self::Ieee { extension: Some(other_ext_id), .. },
            ) => ext_id.cmp(other_ext_id),
            (Self::Vendor { vendor_ie_hdr }, Self::Vendor { vendor_ie_hdr: other_hdr }) => {
                vendor_ie_hdr.cmp(other_hdr)
            }
            _ => self.basic_id().0.cmp(&other.basic_id().0),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ie_type() {
        let basic = IeType::new_basic(Id::SSID);
        let extended = IeType::new_extended(2);
        let vendor = IeType::new_vendor([1, 2, 3, 4, 5, 6]);

        assert_eq!(basic, IeType::Ieee { id: Id::SSID, extension: None });
        assert_eq!(extended, IeType::Ieee { id: Id::EXTENSION, extension: Some(2) });
        assert_eq!(vendor, IeType::Vendor { vendor_ie_hdr: [1, 2, 3, 4, 5, 6] });
        assert_eq!(basic.basic_id(), Id::SSID);
        assert_eq!(extended.basic_id(), Id::EXTENSION);
        assert_eq!(vendor.basic_id(), Id::VENDOR_SPECIFIC);
    }
}
