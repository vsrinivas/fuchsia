// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use Result;
use bytes::{BufMut, BytesMut};
use crypto_utils::prf;
use num::bigint::{BigUint, RandBigInt};
use rand::OsRng;
use time;

#[derive(Debug)]
pub struct NonceReader {
    key_counter: BigUint,
}

impl NonceReader {
    pub fn new(sta_addr: [u8; 6]) -> Result<NonceReader> {
        // Write time and STA's address to buffer for PRF-256.
        // IEEE Std 802.11-2016, 12.7.5 recommends using a time in NTP format.
        // Fuchsia has no support for NTP yet; instead use a regular timestamp.
        // TODO(NET-430): Use time in NTP format once Fuchsia added support.
        let mut buf = BytesMut::with_capacity(14);
        buf.put_u64_le(time::precise_time_ns());
        buf.put_slice(&sta_addr[..]);
        let k = OsRng::new()?.gen_biguint(256).to_bytes_le();
        let init = prf(&k[..], "Init Counter", &buf[..], 256)?;
        Ok(NonceReader {
            key_counter: BigUint::from_bytes_le(&init[..]),
        })
    }

    pub fn next(&mut self) -> Vec<u8> {
        self.key_counter += 1u8;

        // Expand nonce if it's less than 32 bytes.
        let mut result = self.key_counter.to_bytes_le();
        result.resize(32, 0);
        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_next_nonce() {
        let addr: [u8; 6] = [1, 2, 3, 4, 5, 6];
        let result = NonceReader::new(addr);
        assert_eq!(result.is_ok(), true);

        let mut rdr = result.unwrap();
        let mut previous_nonce = rdr.next();
        for _ in 0..300 {
            let nonce = rdr.next();
            let nonce_int = BigUint::from_bytes_le(&nonce[..]);
            let previous_nonce_int = BigUint::from_bytes_le(&previous_nonce[..]);
            assert_eq!(nonce_int.gt(&previous_nonce_int), true);

            previous_nonce = nonce;
        }
    }
}
