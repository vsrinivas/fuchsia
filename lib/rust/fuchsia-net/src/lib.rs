// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde_derive::{Deserialize, Serialize},
    std::fmt,
};

// A hack, since we can't add functionality to the fidl type.
#[derive(PartialEq, Eq, Serialize, Deserialize, Debug)]
pub struct MacAddress {
    pub octets: [u8; 6],
}

impl MacAddress {
    pub fn from_fidl(
        // This destructuring ensures that we don't deviate from the fidl type.
        fidl_zircon_ethernet::MacAddress { octets }: fidl_zircon_ethernet::MacAddress,
    ) -> Self {
        Self { octets }
    }
}

impl fmt::Display for MacAddress {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let Self { octets } = self;
        for (i, byte) in octets.iter().enumerate() {
            if i > 0 {
                write!(f, ":")?;
            }
            write!(f, "{:02x}", byte)?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_display_macaddress() {
        let mac_addr = MacAddress { octets: [0u8, 1u8, 2u8, 255u8, 254u8, 253u8] };
        assert_eq!("00:01:02:ff:fe:fd".to_owned(), format!("{}", mac_addr));
    }
}
