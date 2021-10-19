//! ARMv8 `PMULL`-accelerated implementation of POLYVAL.
//!
//! Based on this C intrinsics implementation:
//! <https://github.com/noloader/AES-Intrinsics/blob/master/clmul-arm.c>
//!
//! Original C written and placed in public domain by Jeffrey Walton.
//! Based on code from ARM, and by Johannes Schneiders, Skip Hovsmith and
//! Barry O'Rourke for the mbedTLS project.
//!
//! For more information about PMULL, see:
//! - <https://developer.arm.com/documentation/100069/0608/A64-SIMD-Vector-Instructions/PMULL--PMULL2--vector->
//! - <https://eprint.iacr.org/2015/688.pdf>

use crate::{Block, Key};
use core::{arch::aarch64::*, mem};
use universal_hash::{consts::U16, NewUniversalHash, Output, UniversalHash};

/// **POLYVAL**: GHASH-like universal hash over GF(2^128).
#[derive(Clone)]
pub struct Polyval {
    h: uint8x16_t,
    y: uint8x16_t,
}

impl NewUniversalHash for Polyval {
    type KeySize = U16;

    /// Initialize POLYVAL with the given `H` field element
    fn new(h: &Key) -> Self {
        unsafe {
            Self {
                h: vld1q_u8(h.as_ptr()),
                y: vdupq_n_u8(0), // all zeroes
            }
        }
    }
}

impl UniversalHash for Polyval {
    type BlockSize = U16;

    #[inline]
    fn update(&mut self, x: &Block) {
        unsafe {
            self.mul(x);
        }
    }

    /// Reset internal state
    fn reset(&mut self) {
        unsafe {
            self.y = vdupq_n_u8(0);
        }
    }

    /// Get GHASH output
    fn finalize(self) -> Output<Self> {
        unsafe { mem::transmute(self.y) }
    }
}

impl Polyval {
    /// Mask value used when performing reduction.
    /// This corresponds to POLYVAL's polynomial with the highest bit unset.
    const MASK: u128 = 1 << 127 | 1 << 126 | 1 << 121 | 1;

    /// POLYVAL carryless multiplication.
    // TODO(tarcieri): investigate ordering optimizations and fusions e.g.`fuse-crypto-eor`
    #[inline]
    #[target_feature(enable = "neon")]
    unsafe fn mul(&mut self, x: &Block) {
        let h = self.h;
        let y = veorq_u8(self.y, vld1q_u8(x.as_ptr()));

        // polynomial multiply
        let z = vdupq_n_u8(0);
        let r0 = pmull::<0, 0>(h, y);
        let r1 = pmull::<1, 1>(h, y);
        let t0 = pmull::<0, 1>(h, y);
        let t1 = pmull::<1, 0>(h, y);
        let t0 = veorq_u8(t0, t1);
        let t1 = vextq_u8(z, t0, 8);
        let r0 = veorq_u8(r0, t1);
        let t1 = vextq_u8(t0, z, 8);
        let r1 = veorq_u8(r1, t1);

        // polynomial reduction
        let p = mem::transmute(Self::MASK);
        let t0 = pmull::<0, 1>(r0, p);
        let t1 = vextq_u8(t0, t0, 8);
        let r0 = veorq_u8(r0, t1);
        let t1 = pmull::<1, 1>(r0, p);
        let r0 = veorq_u8(r0, t1);

        self.y = veorq_u8(r0, r1);
    }
}

/// Wrapper for the ARM64 `PMULL` instruction.
#[inline(always)]
unsafe fn pmull<const A_LANE: i32, const B_LANE: i32>(a: uint8x16_t, b: uint8x16_t) -> uint8x16_t {
    mem::transmute(vmull_p64(
        vgetq_lane_u64(vreinterpretq_u64_u8(a), A_LANE),
        vgetq_lane_u64(vreinterpretq_u64_u8(b), B_LANE),
    ))
}

// TODO(tarcieri): zeroize support
// #[cfg(feature = "zeroize")]
// impl Drop for Polyval {
//     fn drop(&mut self) {
//         use zeroize::Zeroize;
//         self.h.zeroize();
//         self.y.zeroize();
//     }
// }
