// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    boringssl_sys::SHA256_DIGEST_LENGTH,
    ieee80211::Ssid,
    mundane::{
        hash::{Digest, Sha256},
        hmac::hmac,
    },
    static_assertions::const_assert,
};

const HASH_KEY_LEN: usize = 8;

#[derive(Debug, Clone, PartialEq)]
/// Hasher used to hash sensitive information, preserving user privacy.
pub struct WlanHasher {
    hash_key: [u8; HASH_KEY_LEN],
}

impl WlanHasher {
    pub fn new(hash_key: [u8; 8]) -> Self {
        Self { hash_key }
    }

    pub fn hash(&self, bytes: &[u8]) -> [u8; SHA256_DIGEST_LENGTH as usize] {
        hmac::<Sha256>(&self.hash_key, bytes).bytes()
    }

    pub fn hash_mac_addr(&self, mac_addr: &[u8; 6]) -> String {
        format!(
            "{:02x}:{:02x}:{:02x}:{}",
            mac_addr[0],
            mac_addr[1],
            mac_addr[2],
            hex::encode(truncate_sha256_digest_to_8_bytes(&self.hash(&mac_addr[3..6])))
        )
    }

    pub fn hash_ssid(&self, ssid: &Ssid) -> String {
        hex::encode(truncate_sha256_digest_to_8_bytes(&self.hash(ssid)))
    }
}

fn truncate_sha256_digest_to_8_bytes(digest: &[u8; SHA256_DIGEST_LENGTH as usize]) -> [u8; 8] {
    const_assert!(8 <= SHA256_DIGEST_LENGTH);
    let mut res = [0u8; 8];
    res.copy_from_slice(&digest[..8]);
    res
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
        let ssid_foo = Ssid::from("foo");
        let ssid_bar = Ssid::from("bar");
        assert!(!hasher.hash_ssid(&ssid_foo).contains("foo"));
        assert_eq!(hasher.hash_ssid(&ssid_foo), hasher.hash_ssid(&ssid_foo));
        assert_ne!(hasher.hash_ssid(&ssid_foo), hasher.hash_ssid(&ssid_bar));

        // Verify that `&[u8]` argument is accepted.
        assert_eq!(hasher.hash_ssid(&ssid_foo), hasher.hash_ssid(&ssid_foo));
    }

    #[test]
    fn test_truncate_sha256_digest_to_8_bytes() {
        let sha256_digest = hmac::<Sha256>(&[0u8; HASH_KEY_LEN], &[0u8]).bytes();
        let truncated_digest = truncate_sha256_digest_to_8_bytes(&sha256_digest);
        assert_eq!(truncated_digest.len(), 8);
    }
}
