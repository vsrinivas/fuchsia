// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::format::{MacFmt, SsidFmt},
    mundane::{
        hash::{Digest, Sha256},
        hmac::hmac,
    },
};

#[derive(Debug, Clone)]
/// Hasher used to hash sensitive information, preserving user privacy.
pub struct WlanHasher {
    hash_key: [u8; 8],
}

impl WlanHasher {
    pub fn new(hash_key: [u8; 8]) -> Self {
        Self { hash_key }
    }

    pub fn hash(&self, bytes: &[u8]) -> String {
        hex::encode(hmac::<Sha256>(&self.hash_key, bytes).bytes())
    }

    pub fn hash_mac_addr(&self, mac_addr: &impl MacFmt) -> String {
        mac_addr.to_mac_str_partial_hashed(|bytes| self.hash(bytes))
    }

    pub fn hash_ssid(&self, ssid: &impl SsidFmt) -> String {
        ssid.to_ssid_str_hashed(|bytes| self.hash(bytes))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const HASH_KEY_X: [u8; 8] = [1, 2, 3, 4, 5, 6, 7, 8];
    const HASH_KEY_Y: [u8; 8] = [0, 2, 3, 4, 5, 6, 7, 8];

    #[test]
    fn test_hasher() {
        let hasher_x = WlanHasher::new(HASH_KEY_X);
        let hasher_y = WlanHasher::new(HASH_KEY_Y);

        // The same hasher should always get the same result for the same bytes.
        assert_eq!(hasher_x.hash(&[0xa]), hasher_x.hash(&[0xa]));

        // The same hasher should not get the same result for two different sets
        // of bytes with very high probability.
        assert_ne!(hasher_x.hash(&[0xa]), hasher_x.hash(&[0xb]));

        // A different hash key should not get the same result for same bytes
        // with very high probability.
        assert_ne!(hasher_x.hash(&[0xa]), hasher_y.hash(&[0xa]));
    }

    #[test]
    fn test_hash_mac_addr() {
        let hasher = WlanHasher::new(HASH_KEY_X);
        let mac_addr = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66];
        let hashed_str = hasher.hash_mac_addr(&mac_addr);
        assert!(hashed_str.starts_with("11:22:33:"));
        assert_ne!(hashed_str, "11:22:33:44:55:66");
    }

    #[test]
    fn test_hash_ssid() {
        let hasher = WlanHasher::new(HASH_KEY_X);
        let ssid_foo = String::from("foo").into_bytes();
        let ssid_bar = String::from("bar").into_bytes();
        assert!(!hasher.hash_ssid(&ssid_foo).contains("foo"));
        assert_eq!(hasher.hash_ssid(&ssid_foo), hasher.hash_ssid(&ssid_foo));
        assert_ne!(hasher.hash_ssid(&ssid_foo), hasher.hash_ssid(&ssid_bar));
    }
}
