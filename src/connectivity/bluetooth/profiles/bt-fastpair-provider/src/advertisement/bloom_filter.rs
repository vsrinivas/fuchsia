// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sha2::{Digest, Sha256};
use std::convert::TryInto;
use tracing::debug;

use crate::types::{AccountKey, Error};

/// Returns a byte array created by applying a variable-length Bloom Filter to the provided list of
/// Account Keys.
///
/// `keys` is a list of Account Keys and must be nonempty.
/// `salt` is a randomly generated value.
///
/// See https://developers.google.com/nearby/fast-pair/specifications/service/provider#AccountKeyFilter
/// for more details.
///
/// Returns an Error if the input is invalid (empty account keys).
pub fn bloom_filter(keys: &Vec<AccountKey>, salt: u8) -> Result<Vec<u8>, Error> {
    let n = keys.len();
    if n < 1 {
        return Err(Error::internal("Invalid Account Key List size"));
    }

    // The filter size, s, is 1.2*n + 3.
    let s: u8 = (1.2f32 * (n as f32)) as u8 + 3;
    let mut filter = vec![0u8; s.into()];
    debug!("Calculating Bloom filter (length = {}) for {} account keys", s, n);

    for key in keys {
        // The randomly generated `salt` is appended to the Account Key.
        let mut key_with_salt = [0; 17];
        key_with_salt[..16].copy_from_slice(key.as_bytes());
        key_with_salt[16] = salt;

        let mut hasher = Sha256::new();
        hasher.update(key_with_salt);
        let calculated_hash: [u8; 32] = hasher.finalize().into();

        // The hashed Account Key is divided into chunks of 4 byte integer values. Because the
        // hashed key is a 32-byte value, we expect exactly 8 4-byte chunks.
        let x: Vec<u32> = calculated_hash
            .chunks_exact(4)
            .map(|c| u32::from_be_bytes(c.try_into().expect("4 bytes")))
            .collect();

        for x_i in x.into_iter() {
            let m = x_i % (s as u32 * 8);
            let filter_index = m / 8;
            // `filter_index` is guaranteed to be less than `s`.
            // 0 <= m < s*8 (by definition of modulo) => m/8 < s => `filter_index` < s
            filter[filter_index as usize] |= 1 << (m % 8);
        }
    }

    Ok(filter)
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;

    /// Verifies the correctness of the variable-length Bloom filter algorithm. The contents of this
    /// test case are pulled from the GFPS specification.
    /// See https://developers.google.com/nearby/fast-pair/specifications/appendix/testcases#bloom_filter
    #[test]
    fn single_account_key() {
        let example_salt = 0xc7;
        let example_account_key = AccountKey::new([
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xAA, 0xBB, 0xCC, 0xDD,
            0xEE, 0xFF,
        ]);

        let bloom_result =
            bloom_filter(&vec![example_account_key], example_salt).expect("bloom filter succeeds");

        let expected_bloom_result = [0x0a, 0x42, 0x88, 0x10];
        assert_eq!(bloom_result, expected_bloom_result);
    }

    /// Verifies the correctness of the variable-length Bloom filter algorithm. The contents of this
    /// test case are pulled from the GFPS specification.
    /// See https://developers.google.com/nearby/fast-pair/specifications/appendix/testcases#bloom_filter
    #[test]
    fn multiple_account_keys() {
        let example_salt = 0xc7;
        let example_account_key1 = AccountKey::new([
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xAA, 0xBB, 0xCC, 0xDD,
            0xEE, 0xFF,
        ]);
        let example_account_key2 = AccountKey::new([
            0x11, 0x11, 0x22, 0x22, 0x33, 0x33, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77,
            0x88, 0x88,
        ]);

        let bloom_result =
            bloom_filter(&vec![example_account_key1, example_account_key2], example_salt)
                .expect("bloom filter succeeds");

        let expected_bloom_result = [0x2F, 0xBA, 0x06, 0x42, 0x00];
        assert_eq!(bloom_result, expected_bloom_result);
    }

    #[test]
    fn empty_account_key_list_is_error() {
        let example_salt = 0xc7;
        let bloom_result = bloom_filter(&vec![], example_salt);
        assert_matches!(bloom_result, Err(_));
    }
}
