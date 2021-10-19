//! Autodetection for CPU intrinsics, with fallback to the "soft" backend when
//! they are unavailable.

use crate::{backend::soft, Block, Key};
use core::mem::ManuallyDrop;
use universal_hash::{consts::U16, NewUniversalHash, Output, UniversalHash};

#[cfg(all(target_arch = "aarch64", feature = "armv8"))]
use super::pmull as intrinsics;

#[cfg(any(target_arch = "x86_64", target_arch = "x86"))]
use super::clmul as intrinsics;

#[cfg(all(target_arch = "aarch64", feature = "armv8"))]
cpufeatures::new!(mul_intrinsics, "aes"); // `aes` implies PMULL

#[cfg(any(target_arch = "x86_64", target_arch = "x86"))]
cpufeatures::new!(mul_intrinsics, "pclmulqdq", "sse4.1");

/// **POLYVAL**: GHASH-like universal hash over GF(2^128).
pub struct Polyval {
    inner: Inner,
    token: mul_intrinsics::InitToken,
}

union Inner {
    intrinsics: ManuallyDrop<intrinsics::Polyval>,
    soft: ManuallyDrop<soft::Polyval>,
}

impl NewUniversalHash for Polyval {
    type KeySize = U16;

    /// Initialize POLYVAL with the given `H` field element
    fn new(h: &Key) -> Self {
        let (token, has_intrinsics) = mul_intrinsics::init_get();

        let inner = if has_intrinsics {
            Inner {
                intrinsics: ManuallyDrop::new(intrinsics::Polyval::new(h)),
            }
        } else {
            Inner {
                soft: ManuallyDrop::new(soft::Polyval::new(h)),
            }
        };

        Self { inner, token }
    }
}

impl UniversalHash for Polyval {
    type BlockSize = U16;

    /// Input a field element `X` to be authenticated
    #[inline]
    fn update(&mut self, x: &Block) {
        if self.token.get() {
            unsafe { (*self.inner.intrinsics).update(x) }
        } else {
            unsafe { (*self.inner.soft).update(x) }
        }
    }

    /// Reset internal state
    fn reset(&mut self) {
        if self.token.get() {
            unsafe { (*self.inner.intrinsics).reset() }
        } else {
            unsafe { (*self.inner.soft).reset() }
        }
    }

    /// Get POLYVAL result (i.e. computed `S` field element)
    fn finalize(self) -> Output<Self> {
        let output_bytes = if self.token.get() {
            unsafe {
                ManuallyDrop::into_inner(self.inner.intrinsics)
                    .finalize()
                    .into_bytes()
            }
        } else {
            unsafe {
                ManuallyDrop::into_inner(self.inner.soft)
                    .finalize()
                    .into_bytes()
            }
        };

        Output::new(output_bytes)
    }
}

impl Clone for Polyval {
    fn clone(&self) -> Self {
        let inner = if self.token.get() {
            Inner {
                intrinsics: ManuallyDrop::new(unsafe { (*self.inner.intrinsics).clone() }),
            }
        } else {
            Inner {
                soft: ManuallyDrop::new(unsafe { (*self.inner.soft).clone() }),
            }
        };

        Self {
            inner,
            token: self.token,
        }
    }
}
