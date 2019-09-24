// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    fidl_fuchsia_bluetooth as fidl,
    std::{convert::TryFrom, fmt},
};

/// A Bluetooth device id. The peer id used in Fuchsia Bluetooth.
/// `PeerId` can be converted to/from a FIDL Bluetooth PeerId type.
#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
struct PeerId {
    value: u64,
}

impl PeerId {
    pub fn new(id: u64) -> Self {
        Self { value: id }
    }

    /// Zero-Pad the output string to be 16 characters to maintain formatting consistency.
    pub fn to_string(&self) -> String {
        format!("{:016x}", self.value)
    }

    pub fn to_fidl(&self) -> fidl::PeerId {
        fidl::PeerId { value: self.value }
    }
}

impl From<fidl::PeerId> for PeerId {
    fn from(src: fidl::PeerId) -> PeerId {
        PeerId::new(src.value)
    }
}

impl Into<fidl::PeerId> for PeerId {
    fn into(self) -> fidl::PeerId {
        self.to_fidl()
    }
}

impl TryFrom<String> for PeerId {
    type Error = failure::Error;

    /// Valid id strings have only Hex characters (0-9, a-f) and are 16 chars long
    /// to match the 64 bit representation of a PeerId.
    fn try_from(src: String) -> Result<PeerId, Error> {
        if src.len() > 16 {
            return Err(format_err!("Failed to convert String to PeerId"));
        }
        match u64::from_str_radix(&src, 16) {
            Ok(id) => Ok(PeerId::new(id)),
            Err(_) => Err(format_err!("Failed to convert String to PeerId")),
        }
    }
}

impl Into<String> for PeerId {
    fn into(self) -> String {
        self.to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn peerid_to_string() {
        // Lowest possible id
        let id = PeerId::new(0);
        let res = id.to_string();
        assert_eq!("0000000000000000", res);

        // Normal case with padding
        let id = PeerId::new(1234567890);
        let res = id.to_string();
        assert_eq!("00000000499602d2", res);

        // Normal case with padding
        let id = PeerId::new(123123777771778888);
        let res = id.to_string();
        assert_eq!("01b56c6c6d7db348", res);

        // Normal case without padding
        let id = PeerId::new(2000037777717788818);
        let res = id.to_string();
        assert_eq!("1bc18fc31e3b0092", res);

        // u64 max test
        let id = PeerId::new(std::u64::MAX);
        let res = id.to_string();
        assert_eq!("ffffffffffffffff", res);
    }

    #[test]
    fn peerid_from_string() {
        // Largest valid id.
        let str_id = "ffffffffffffffff".to_string();
        let peer_id = PeerId::try_from(str_id).expect("String to PeerId failed.");
        assert_eq!(PeerId::new(18446744073709551615), peer_id);

        // Smallest valid id.
        let str_id = "0000000000000000".to_string();
        let peer_id = PeerId::try_from(str_id).expect("String to PeerId failed.");
        assert_eq!(PeerId::new(0), peer_id);

        // BT stack wont produce IDs that aren't 16 characters long, but the conversion
        // can handle smaller string ids.
        // In the reverse direction, the string will be padded to 16 characters.
        let str_id = "10".to_string();
        let peer_id = PeerId::try_from(str_id).expect("String to PeerId failed.");
        assert_eq!(PeerId::new(16), peer_id);

        // Normal case.
        let str_id = "fe12ffdda3b89002".to_string();
        let peer_id = PeerId::try_from(str_id).expect("String to PeerId failed.");
        assert_eq!(PeerId::new(18307976762614124546), peer_id);

        // String with invalid hex chars (i.e not 0-9, A-F).
        let str_id = "klinvalidstr".to_string();
        let peer_id = PeerId::try_from(str_id);
        assert!(peer_id.is_err());

        // String that is too long to be a PeerId (> 16 chars).
        let str_id = "90000111122223333".to_string();
        let peer_id = PeerId::try_from(str_id);
        assert!(peer_id.is_err());
    }
    #[test]
    fn peerid_into_fidl() {
        let peer_id = PeerId::new(1234567);
        let fidl_id: fidl::PeerId = peer_id.into();
        assert_eq!(1234567, fidl_id.value);
    }
    #[test]
    fn peerid_from_fidl() {
        let fidl_id = fidl::PeerId { value: 999999988 };
        let peer_id: PeerId = fidl_id.into();
        assert_eq!(PeerId::new(999999988), peer_id);
    }
}
