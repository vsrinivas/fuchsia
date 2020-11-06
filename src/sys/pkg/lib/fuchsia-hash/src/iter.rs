// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{Hash, HASH_SIZE};

/// Iterator over all 2^256 possible hash values. Not expected to be useful outside of tests.
#[derive(Debug)]
pub struct HashRangeFull(Option<[u8; HASH_SIZE]>);

impl Default for HashRangeFull {
    fn default() -> Self {
        Self(Some([0; HASH_SIZE]))
    }
}

impl Iterator for HashRangeFull {
    type Item = Hash;

    fn next(&mut self) -> Option<Self::Item> {
        fn inc(mut bignum: [u8; HASH_SIZE]) -> Option<[u8; HASH_SIZE]> {
            let mut bytes = bignum.iter_mut().rev();
            loop {
                let n = bytes.next()?;
                let (next, overflowed) = n.overflowing_add(1);
                *n = next;
                if !overflowed {
                    break;
                }
            }
            Some(bignum)
        }

        match self.0 {
            Some(bytes) => {
                let res = Hash::from(bytes);
                self.0 = inc(bytes);
                Some(res)
            }
            None => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hash_range_full() {
        let mut iter = HashRangeFull::default();
        assert_eq!(
            iter.next(),
            Some(Hash::from([
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0
            ]))
        );
        assert_eq!(
            iter.next(),
            Some(Hash::from([
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 1
            ]))
        );

        assert_eq!(
            HashRangeFull::default().nth(256),
            Some(Hash::from([
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 1, 0
            ]))
        );
    }

    #[test]
    fn hash_range_full_ends() {
        let mut iter = HashRangeFull(Some([255; HASH_SIZE]));
        assert_eq!(iter.next(), Some(Hash::from([255; HASH_SIZE])));
        assert_eq!(iter.next(), None);
    }
}
