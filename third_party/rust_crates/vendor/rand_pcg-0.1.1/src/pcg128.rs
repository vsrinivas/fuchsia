// Copyright 2018 Developers of the Rand project.
// Copyright 2017 Paul Dicker.
// Copyright 2014-2017 Melissa O'Neill and PCG Project contributors
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// https://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! PCG random number generators

// This is the default multiplier used by PCG for 64-bit state.
const MULTIPLIER: u128 = 2549297995355413924u128 << 64 | 4865540595714422341;

use core::fmt;
use core::mem::transmute;
use rand_core::{RngCore, SeedableRng, Error, le};

/// A PCG random number generator (XSL 128/64 (MCG) variant).
/// 
/// Permuted Congruential Generator with 128-bit state, internal Multiplicative
/// Congruential Generator, and 64-bit output via "xorshift low (bits),
/// random rotation" output function.
/// 
/// This is a 128-bit MCG with the PCG-XSL-RR output function.
/// Note that compared to the standard `pcg64` (128-bit LCG with PCG-XSL-RR
/// output function), this RNG is faster, also has a long cycle, and still has
/// good performance on statistical tests.
/// 
/// Note: this RNG is only available using Rust 1.26 or later.
#[derive(Clone)]
#[cfg_attr(feature="serde1", derive(Serialize,Deserialize))]
pub struct Mcg128Xsl64 {
    state: u128,
}

/// A friendly name for `Mcg128Xsl64`.
pub type Pcg64Mcg = Mcg128Xsl64;

impl Mcg128Xsl64 {
    /// Construct an instance compatible with PCG seed.
    /// 
    /// Note that PCG specifies a default value for the parameter:
    /// 
    /// - `state = 0xcafef00dd15ea5e5`
    pub fn new(state: u128) -> Self {
        // Force low bit to 1, as in C version (C++ uses `state | 3` instead).
        Mcg128Xsl64 { state: state | 1 }
    }
}

// Custom Debug implementation that does not expose the internal state
impl fmt::Debug for Mcg128Xsl64 {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "Mcg128Xsl64 {{}}")
    }
}

/// We use a single 126-bit seed to initialise the state and select a stream.
/// Two `seed` bits (lowest order of last byte) are ignored.
impl SeedableRng for Mcg128Xsl64 {
    type Seed = [u8; 16];

    fn from_seed(seed: Self::Seed) -> Self {
        // Read as if a little-endian u128 value:
        let mut seed_u64 = [0u64; 2];
        le::read_u64_into(&seed, &mut seed_u64);
        let state = (seed_u64[0] as u128) |
                    (seed_u64[1] as u128) << 64;
        Mcg128Xsl64::new(state)
    }
}

impl RngCore for Mcg128Xsl64 {
    #[inline]
    fn next_u32(&mut self) -> u32 {
        self.next_u64() as u32
    }

    #[inline]
    fn next_u64(&mut self) -> u64 {
        // prepare the LCG for the next round
        let state = self.state.wrapping_mul(MULTIPLIER);
        self.state = state;

        // Output function XSL RR ("xorshift low (bits), random rotation")
        // Constants are for 128-bit state, 64-bit output
        const XSHIFT: u32 = 64;     // (128 - 64 + 64) / 2
        const ROTATE: u32 = 122;    // 128 - 6

        let rot = (state >> ROTATE) as u32;
        let xsl = ((state >> XSHIFT) as u64) ^ (state as u64);
        xsl.rotate_right(rot)
    }

    #[inline]
    fn fill_bytes(&mut self, dest: &mut [u8]) {
        // specialisation of impls::fill_bytes_via_next; approx 3x faster
        let mut left = dest;
        while left.len() >= 8 {
            let (l, r) = {left}.split_at_mut(8);
            left = r;
            let chunk: [u8; 8] = unsafe {
                transmute(self.next_u64().to_le())
            };
            l.copy_from_slice(&chunk);
        }
        let n = left.len();
        if n > 0 {
            let chunk: [u8; 8] = unsafe {
                transmute(self.next_u64().to_le())
            };
            left.copy_from_slice(&chunk[..n]);
        }
    }

    #[inline]
    fn try_fill_bytes(&mut self, dest: &mut [u8]) -> Result<(), Error> {
        Ok(self.fill_bytes(dest))
    }
}

#[cfg(test)]
mod tests {
    use ::rand_core::{RngCore, SeedableRng};
    use super::*;

    #[test]
    fn test_mcg128xsl64_construction() {
        // Test that various construction techniques produce a working RNG.
        let seed = [1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16];
        let mut rng1 = Mcg128Xsl64::from_seed(seed);
        assert_eq!(rng1.next_u64(), 7071994460355047496);

        let mut rng2 = Mcg128Xsl64::from_rng(&mut rng1).unwrap();
        assert_eq!(rng2.next_u64(), 12300796107712034932);

        let mut rng3 = Mcg128Xsl64::seed_from_u64(0);
        assert_eq!(rng3.next_u64(), 6198063878555692194);

        // This is the same as Mcg128Xsl64, so we only have a single test:
        let mut rng4 = Pcg64Mcg::seed_from_u64(0);
        assert_eq!(rng4.next_u64(), 6198063878555692194);
    }

    #[test]
    fn test_mcg128xsl64_true_values() {
        // Numbers copied from official test suite (C version).
        let mut rng = Mcg128Xsl64::new(42);

        let mut results = [0u64; 6];
        for i in results.iter_mut() { *i = rng.next_u64(); }
        let expected: [u64; 6] = [0x63b4a3a813ce700a, 0x382954200617ab24,
            0xa7fd85ae3fe950ce, 0xd715286aa2887737, 0x60c92fee2e59f32c, 0x84c4e96beff30017];
        assert_eq!(results, expected);
    }

    #[cfg(feature="serde1")]
    #[test]
    fn test_mcg128xsl64_serde() {
        use bincode;
        use std::io::{BufWriter, BufReader};

        let mut rng = Mcg128Xsl64::seed_from_u64(0);

        let buf: Vec<u8> = Vec::new();
        let mut buf = BufWriter::new(buf);
        bincode::serialize_into(&mut buf, &rng).expect("Could not serialize");

        let buf = buf.into_inner().unwrap();
        let mut read = BufReader::new(&buf[..]);
        let mut deserialized: Mcg128Xsl64 = bincode::deserialize_from(&mut read).expect("Could not deserialize");

        assert_eq!(rng.state, deserialized.state);

        for _ in 0..16 {
            assert_eq!(rng.next_u64(), deserialized.next_u64());
        }
    }
}
