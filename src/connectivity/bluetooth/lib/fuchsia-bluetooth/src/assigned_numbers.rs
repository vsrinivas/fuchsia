// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use self::constants::{
    CHARACTERISTIC_NUMBERS, CUSTOM_SERVICE_UUIDS, DESCRIPTOR_NUMBERS, SERVICE_UUIDS,
};
use crate::types::Uuid;

pub(crate) mod constants;

/// An assigned number, code, or identifier for a concept in the Bluetooth wireless standard.
/// Includes an associated abbreviation and human-readable name for the number.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AssignedNumber {
    /// 16-bit Bluetooth UUID.
    pub number: u16,
    /// Short abbreviation of the `name`
    pub abbreviation: Option<&'static str>,
    /// Human readable name
    pub name: &'static str,
}

impl AssignedNumber {
    /// Tests if an `identifier` matches any of the fields in the assigned number.
    /// Matches are case-insensitive. Matches on the number can be in the canonical
    /// 8-4-4-4-12 uuid string format or the first 8 digits of the uuid with or without
    /// leading 0s.
    pub fn matches(&self, identifier: &str) -> bool {
        let identifier = &identifier.to_uppercase();
        self.matches_abbreviation(identifier)
            || self.matches_name(identifier)
            || self.matches_number(identifier)
    }

    fn matches_abbreviation(&self, identifier: &str) -> bool {
        self.abbreviation.map(|abbr| abbr == identifier).unwrap_or(false)
    }

    fn matches_name(&self, identifier: &str) -> bool {
        self.name.to_uppercase() == identifier
    }

    /// Matches full uuid or short form of Bluetooth SIG assigned numbers.
    /// Precondition: identifier should already be uppercase when passed into the method.
    fn matches_number(&self, identifier: &str) -> bool {
        let identifier = if identifier.starts_with("0X") { &identifier[2..] } else { identifier };
        let string = Uuid::new16(self.number).to_string().to_uppercase();
        if identifier.len() == 32 + 4 {
            identifier == string
        } else {
            let short_form =
                string.split("-").next().expect("split iter always has at least 1 item");
            // pad out the identifier with leading zeros to a width of 8
            short_form == format!("{:0>8}", identifier)
        }
    }
}

/// Search for the Bluetooth SIG number for a given service identifier
/// and return associated information. `identifier` can be a human readable
/// string, abbreviation, or the number in full uuid format or shortened forms
pub fn find_service_uuid(identifier: &str) -> Option<AssignedNumber> {
    SERVICE_UUIDS
        .iter()
        .chain(CUSTOM_SERVICE_UUIDS.iter())
        .find(|sn| sn.matches(identifier))
        .copied()
}

/// Search for the Bluetooth SIG number for a given characteristic identifier
/// and return associated information. `identifier` can be a human readable
/// string or the number in full uuid format or shortened forms
pub fn find_characteristic_number(identifier: &str) -> Option<AssignedNumber> {
    CHARACTERISTIC_NUMBERS.iter().find(|cn| cn.matches(identifier)).map(|&an| an)
}

/// Search for the Bluetooth SIG number for a given descriptor identifier
/// and return associated information. `identifier` can be a human readable
/// string or the number in full uuid format or shortened forms
pub fn find_descriptor_number(identifier: &str) -> Option<AssignedNumber> {
    DESCRIPTOR_NUMBERS.iter().find(|dn| dn.matches(identifier)).map(|&an| an)
}

#[macro_export]
macro_rules! assigned_number {
    ($num:expr, $abbr:expr, $name:expr) => {
        AssignedNumber { number: $num, abbreviation: Some($abbr), name: $name }
    };
    ($num:expr, $name:expr) => {
        AssignedNumber { number: $num, abbreviation: None, name: $name }
    };
}

#[cfg(test)]
mod tests {
    use super::{
        find_characteristic_number, find_descriptor_number, find_service_uuid,
        CHARACTERISTIC_NUMBERS, CUSTOM_SERVICE_UUIDS, DESCRIPTOR_NUMBERS, SERVICE_UUIDS,
    };

    #[test]
    fn test_find_characteristic_number() {
        assert_eq!(find_characteristic_number("Device Name"), Some(CHARACTERISTIC_NUMBERS[0]));
        assert_eq!(find_characteristic_number("aPPEARANCE"), Some(CHARACTERISTIC_NUMBERS[1]));
        assert_eq!(find_characteristic_number("fake characteristic name"), None);
        assert_eq!(find_characteristic_number("2a00"), Some(CHARACTERISTIC_NUMBERS[0]));
        assert_eq!(find_characteristic_number("zzzz"), None);
    }

    #[test]
    fn test_find_descriptor_number() {
        assert_eq!(find_descriptor_number("Report reference"), Some(DESCRIPTOR_NUMBERS[8]));
        assert_eq!(find_descriptor_number("fake descriptor name"), None);
        assert_eq!(
            find_descriptor_number("00002908-0000-1000-8000-00805F9B34FB"),
            Some(DESCRIPTOR_NUMBERS[8])
        );
        assert_eq!(find_descriptor_number("2908"), Some(DESCRIPTOR_NUMBERS[8]));
        assert_eq!(find_descriptor_number("zzzz"), None);
    }

    #[test]
    fn test_find_service_uuid() {
        assert_eq!(find_service_uuid("GAP"), Some(SERVICE_UUIDS[0]));
        assert_eq!(find_service_uuid("Gap"), Some(SERVICE_UUIDS[0]));
        assert_eq!(find_service_uuid("gap"), Some(SERVICE_UUIDS[0]));
        assert_eq!(find_service_uuid("hIdS"), Some(SERVICE_UUIDS[16]));
        assert_eq!(find_service_uuid("XYZ"), None);
        assert_eq!(find_service_uuid("Human Interface Device Service"), Some(SERVICE_UUIDS[16]));
        assert_eq!(find_service_uuid("Fake Service Name"), None);
        assert_eq!(find_service_uuid("183A"), Some(SERVICE_UUIDS[39]));
        assert_eq!(find_service_uuid("0x183a"), Some(SERVICE_UUIDS[39]));
        assert_eq!(find_service_uuid("0000183a"), Some(SERVICE_UUIDS[39]));
        assert_eq!(
            find_service_uuid("0000183A-0000-1000-8000-00805F9B34FB"),
            Some(SERVICE_UUIDS[39])
        );
        assert_eq!(
            find_service_uuid("0000183a-0000-1000-8000-00805f9b34fb"),
            Some(SERVICE_UUIDS[39])
        );
        assert_eq!(find_service_uuid("0000183A-0000-1000-8000-000000000000"), None);
        assert_eq!(find_service_uuid("ZZZZZZZZ"), None);
        assert_eq!(find_service_uuid("ZZZZZZZZ-0000-1000-8000-00805F9B34FB"), None);
        // found in CUSTOM_SERVICE_UUIDS
        assert_eq!(find_service_uuid("fdcf"), Some(CUSTOM_SERVICE_UUIDS[0]));
        assert_eq!(find_service_uuid("FDE2"), Some(CUSTOM_SERVICE_UUIDS[19]));
    }
}
