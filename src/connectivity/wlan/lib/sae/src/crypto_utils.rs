// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {mundane::hash::Digest, mundane::hmac::HmacSha256};

// IEEE Std 802.11-2016, 12.7.1.7.2
// KDF-Hash-Length with Hash set to SHA256.
#[allow(unused)]
pub fn kdf_sha256(k: &[u8], label: &str, context: &[u8], bits: u16) -> Vec<u8> {
    let iterations: u16 = (bits + 255) / 256;
    let mut result = Vec::with_capacity((iterations * 256 / 8) as usize);
    for i in 1u16..=iterations {
        let mut hmac = HmacSha256::new(k);
        hmac.update(&i.to_le_bytes()[..]);
        hmac.update(label.as_bytes());
        hmac.update(context);
        hmac.update(&bits.to_le_bytes()[..]);
        result.extend_from_slice(&hmac.finish().bytes()[..]);
    }
    result.truncate((bits / 8) as usize);
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex::FromHex;

    // IEEE 802.11-18/1104r0: Deriving SAE password element. KDF-Hash-Length does not have published
    // test vectors, so we instead use examples from SAE test vectors.
    #[test]
    fn test_kdf_sha256_256() {
        let key = Vec::from_hex("b39919ad439eee7db61d9564421700acb9ed1044577ce9345e38c97de673c3f0")
            .unwrap();
        // IEEE Std 802.11-2016 12.4.4.2.2 defines this label
        let label = "SAE Hunting and Pecking";
        // This is the prime order of the 256-bit EC group specified by RFC 5903.
        let context =
            Vec::from_hex("ffffffff00000001000000000000000000000000ffffffffffffffffffffffff")
                .unwrap();
        let hash = kdf_sha256(&key[..], label, &context[..], 256);
        let expected =
            Vec::from_hex("282167aa6ba380c53a9c5230c0b53b1ebc456983cc05f0f2b869a75f7f232e91")
                .unwrap();
        assert_eq!(hash, expected);
    }

    // IEEE 802.11-18/1104r0: Deriving SAE KCK and PMK.
    #[test]
    fn test_kdf_sha256_512() {
        let key = Vec::from_hex("7457a00754dcc4e3dc2850c124d6bb8fa1699d7fa33bb0667d9c34eeb513deb9")
            .unwrap();
        // IEEE Std 802.11-2016 12.4.5.4 defines this label
        let label = "SAE KCK and PMK";
        let context =
            Vec::from_hex("40a09b6017cebf0072843b5352aa2b4fb1a84a84c0fe5c235896466a2d3418b1")
                .unwrap();
        let hash = kdf_sha256(&key[..], label, &context[..], 512);
        // This was computed by hand using the algorithm from IEEE Std 802.11-2016 12.4.5.4 and the
        // intermediate values from IEEE 802.11-18/1104r0.
        let expected = Vec::from_hex("599d6f1e27548be8499dceed2feccf94818ce1c79f1b4eb3d6a53228a09bf3ed7aead86fba4c3221fc437f5f14d70d854ea5d5aac1690116793081eda4d557c5").unwrap();
        assert_eq!(hash, expected);
    }

    #[test]
    fn test_kdf_sha256_short() {
        let key = Vec::from_hex("f0f0f0f0").unwrap();
        let label = "LABELED!";
        let context = Vec::from_hex("babababa").unwrap();
        let hash = kdf_sha256(&key[..], label, &context[..], 128);
        assert_eq!(hash.len(), 16);
    }

    #[test]
    fn test_kdf_sha256_empty_data() {
        let key = Vec::from_hex("f0f0f0f0").unwrap();
        let label = "LABELED!";
        let context = Vec::from_hex("babababa").unwrap();
        let hash = kdf_sha256(&key[..], label, &context[..], 0);
        assert_eq!(hash.len(), 0);
    }

    #[test]
    fn test_kdf_sha256_all_empty() {
        let key = vec![];
        let label = "";
        let context = vec![];
        let hash = kdf_sha256(&key[..], label, &context[..], 0);
        assert_eq!(hash.len(), 0);
    }
}
