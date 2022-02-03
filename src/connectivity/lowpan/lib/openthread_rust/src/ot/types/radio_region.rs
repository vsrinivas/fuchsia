// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Represents a radio regulatory region/domain.
///
/// See [`crate::Radio::get_region()`] and [`crate::Radio::set_region()`].
#[derive(Debug, Hash, Clone, Eq, Default, PartialEq, Copy)]
pub struct RadioRegion(u16);

impl RadioRegion {
    /// Creates an instance of `RadioRegion` from an array of two bytes.
    pub fn from_bytes(bytes: [u8; 2]) -> RadioRegion {
        RadioRegion(((bytes[0] as u16) << 8) + bytes[1] as u16)
    }

    /// Returns the value of this `RadioRegion` as an array of two bytes.
    pub fn bytes(&self) -> [u8; 2] {
        [(self.0 >> 8) as u8, self.0 as u8]
    }
}

impl From<u16> for RadioRegion {
    fn from(x: u16) -> Self {
        RadioRegion(x)
    }
}

impl From<RadioRegion> for u16 {
    fn from(x: RadioRegion) -> Self {
        x.0
    }
}

impl From<[u8; 2]> for RadioRegion {
    fn from(x: [u8; 2]) -> Self {
        RadioRegion::from_bytes(x)
    }
}

impl From<RadioRegion> for [u8; 2] {
    fn from(x: RadioRegion) -> Self {
        x.bytes()
    }
}

impl TryFrom<&str> for RadioRegion {
    type Error = anyhow::Error;
    fn try_from(region: &str) -> Result<Self, Self::Error> {
        if region.is_empty() {
            return Ok(RadioRegion::default());
        }
        if region.len() == 2 {
            let ret = RadioRegion::from_bytes([
                region.bytes().nth(0).unwrap(),
                region.bytes().nth(1).unwrap(),
            ]);
            return Ok(ret);
        }
        Err(anyhow::format_err!("Bad region string {:?}", region))
    }
}

impl TryFrom<String> for RadioRegion {
    type Error = anyhow::Error;
    fn try_from(region: String) -> Result<Self, Self::Error> {
        RadioRegion::try_from(region.as_str())
    }
}

impl ToString for RadioRegion {
    fn to_string(&self) -> String {
        if self.0 == 0 {
            return String::default();
        }
        core::str::from_utf8(&self.bytes()).unwrap_or("").to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;

    #[test]
    fn test_region_code() {
        assert_eq!(RadioRegion::from(0), RadioRegion::default());
        assert_eq!(RadioRegion::from(0).to_string(), "".to_string());
        assert_eq!(RadioRegion::from_bytes([0x34, 0x32]).to_string(), "42".to_string());
        assert_eq!(RadioRegion::try_from("US").unwrap().to_string(), "US".to_string());
        assert_eq!(RadioRegion::try_from("").unwrap(), RadioRegion::default());
        assert_matches!(RadioRegion::try_from("1"), Err(_));
        assert_matches!(RadioRegion::try_from("111"), Err(_));
    }
}
