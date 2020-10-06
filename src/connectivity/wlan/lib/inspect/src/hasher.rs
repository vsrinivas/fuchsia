// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    mundane::{
        hash::{Digest, Sha256},
        hmac::hmac,
    },
    wlan_common::{format::MacFmt, mac::MacAddr},
};

#[derive(Debug, Clone)]
/// Hasher used to hash sensitive information, preserving user privacy.
pub struct InspectHasher {
    hash_key: [u8; 8],
}

impl InspectHasher {
    pub fn new(hash_key: [u8; 8]) -> Self {
        Self { hash_key }
    }

    pub fn hash(&self, bytes: &[u8]) -> String {
        hex::encode(hmac::<Sha256>(&self.hash_key, bytes).bytes())
    }

    pub fn hash_mac_addr(&self, addr: MacAddr) -> String {
        addr.to_mac_str_partial_hashed(|bytes| {
            hex::encode(hmac::<Sha256>(&self.hash_key, bytes).bytes())
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const HASH_KEY: [u8; 8] = [1, 2, 3, 4, 5, 6, 7, 8];

    #[test]
    fn test_hash_mac_addr() {
        let hasher = InspectHasher::new(HASH_KEY);
        let mac_addr = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66];
        let hashed_str = hasher.hash_mac_addr(mac_addr);
        assert!(hashed_str.starts_with("11:22:33:"));
        assert_ne!(hashed_str, "11:22:33:44:55:66");
    }
}
