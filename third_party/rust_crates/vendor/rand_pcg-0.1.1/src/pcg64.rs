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

use core::fmt;
use core::mem::transmute;
use rand_core::{RngCore, SeedableRng, Error, le, impls};

// This is the default multiplier used by PCG for 64-bit state.
const MULTIPLIER: u64 = 6364136223846793005;

/// A PCG random number generator (XSH RR 64/32 (LCG) variant).
/// 
/// Permuted Congruential Generator with 64-bit state, internal Linear
/// Congruential Generator, and 32-bit output via "xorshift high (bits),
/// random rotation" output function.
/// 
/// This is a 64-bit LCG with explicitly chosen stream with the PCG-XSH-RR
/// output function. This combination is the standard `pcg32`.
/// 
/// Despite the name, this implementation uses 16 bytes (128 bit) space
/// comprising 64 bits of state and 64 bits stream selector. These are both set
/// by `SeedableRng`, using a 128-bit seed.
#[derive(Clone)]
#[cfg_attr(feature="serde1", derive(Serialize,Deserialize))]
pub struct Lcg64Xsh32 {
    state: u64,
    increment: u64,
}

/// `Lcg64Xsh32` is also officially known as `pcg32`.
pub type Pcg32 = Lcg64Xsh32;

impl Lcg64Xsh32 {
    /// Construct an instance compatible with PCG seed and stream.
    /// 
    /// Note that PCG specifies default values for both parameters:
    /// 
    /// - `state = 0xcafef00dd15ea5e5`
    /// - `stream = 721347520444481703`
    pub fn new(state: u64, stream: u64) -> Self {
        // The increment must be odd, hence we discard one bit:
        let increment = (stream << 1) | 1;
        Lcg64Xsh32::from_state_incr(state, increment)
    }
    
    #[inline]
    fn from_state_incr(state: u64, increment: u64) -> Self {
        let mut pcg = Lcg64Xsh32 { state, increment };
        // Move away from inital value:
        pcg.state = pcg.state.wrapping_add(pcg.increment);
        pcg.step();
        pcg
    }
    
    #[inline]
    fn step(&mut self) {
        // prepare the LCG for the next round
        self.state = self.state
            .wrapping_mul(MULTIPLIER)
            .wrapping_add(self.increment);
    }
}

// Custom Debug implementation that does not expose the internal state
impl fmt::Debug for Lcg64Xsh32 {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "Lcg64Xsh32 {{}}")
    }
}

/// We use a single 127-bit seed to initialise the state and select a stream.
/// One `seed` bit (lowest bit of `seed[8]`) is ignored.
impl SeedableRng for Lcg64Xsh32 {
    type Seed = [u8; 16];

    fn from_seed(seed: Self::Seed) -> Self {
        let mut seed_u64 = [0u64; 2];
        le::read_u64_into(&seed, &mut seed_u64);

        // The increment must be odd, hence we discard one bit:
        Lcg64Xsh32::from_state_incr(seed_u64[0], seed_u64[1] | 1)
    }
}

impl RngCore for Lcg64Xsh32 {
    #[inline]
    fn next_u32(&mut self) -> u32 {
        let state = self.state;
        self.step();

        // Output function XSH RR: xorshift high (bits), followed by a random rotate
        // Constants are for 64-bit state, 32-bit output
        const ROTATE: u32 = 59; // 64 - 5
        const XSHIFT: u32 = 18; // (5 + 32) / 2
        const SPARE: u32 = 27;  // 64 - 32 - 5

        let rot = (state >> ROTATE) as u32;
        let xsh = (((state >> XSHIFT) ^ state) >> SPARE) as u32;
        xsh.rotate_right(rot)
    }

    #[inline]
    fn next_u64(&mut self) -> u64 {
        impls::next_u64_via_u32(self)
    }

    #[inline]
    fn fill_bytes(&mut self, dest: &mut [u8]) {
        // specialisation of impls::fill_bytes_via_next; approx 40% faster
        let mut left = dest;
        while left.len() >= 4 {
            let (l, r) = {left}.split_at_mut(4);
            left = r;
            let chunk: [u8; 4] = unsafe {
                transmute(self.next_u32().to_le())
            };
            l.copy_from_slice(&chunk);
        }
        let n = left.len();
        if n > 0 {
            let chunk: [u8; 4] = unsafe {
                transmute(self.next_u32().to_le())
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
    fn test_lcg64xsh32_construction() {
        // Test that various construction techniques produce a working RNG.
        let seed = [1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16];
        let mut rng1 = Lcg64Xsh32::from_seed(seed);
        assert_eq!(rng1.next_u64(), 1204678643940597513);

        let mut rng2 = Lcg64Xsh32::from_rng(&mut rng1).unwrap();
        assert_eq!(rng2.next_u64(), 12384929573776311845);

        let mut rng3 = Lcg64Xsh32::seed_from_u64(0);
        assert_eq!(rng3.next_u64(), 18195738587432868099);

        // This is the same as Lcg64Xsh32, so we only have a single test:
        let mut rng4 = Pcg32::seed_from_u64(0);
        assert_eq!(rng4.next_u64(), 18195738587432868099);
    }

    #[test]
    fn test_lcg64xsh32_true_values() {
        // Numbers copied from official test suite.
        let mut rng = Lcg64Xsh32::new(42, 54);

        let mut results = [0u32; 6];
        for i in results.iter_mut() { *i = rng.next_u32(); }
        let expected: [u32; 6] = [0xa15c02b7, 0x7b47f409, 0xba1d3330,
            0x83d2f293, 0xbfa4784b, 0xcbed606e];
        assert_eq!(results, expected);
    }

    #[cfg(feature="serde1")]
    #[test]
    fn test_lcg64xsh32_serde() {
        use bincode;
        use std::io::{BufWriter, BufReader};

        let mut rng = Lcg64Xsh32::seed_from_u64(0);

        let buf: Vec<u8> = Vec::new();
        let mut buf = BufWriter::new(buf);
        bincode::serialize_into(&mut buf, &rng).expect("Could not serialize");

        let buf = buf.into_inner().unwrap();
        let mut read = BufReader::new(&buf[..]);
        let mut deserialized: Lcg64Xsh32 = bincode::deserialize_from(&mut read).expect("Could not deserialize");

        assert_eq!(rng.state, deserialized.state);

        for _ in 0..16 {
            assert_eq!(rng.next_u64(), deserialized.next_u64());
        }
    }
}
