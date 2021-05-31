// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    arch::x86_64::*,
    ops::{
        Add, AddAssign, BitAnd, BitOr, BitOrAssign, BitXor, Div, Mul, MulAssign, Neg, Not, Shr, Sub,
    },
    ptr,
};

#[derive(Clone, Copy, Debug)]
pub struct m8x16(__m128i);

impl m8x16 {
    pub fn all(self) -> bool {
        unsafe { _mm_movemask_epi8(_mm_cmpeq_epi8(self.0, _mm_setzero_si128())) == 0 }
    }
}

impl Default for m8x16 {
    fn default() -> Self {
        Self(unsafe { _mm_setzero_si128() })
    }
}

#[derive(Clone, Copy, Debug)]
pub struct m32x8(__m256i);

impl m32x8 {
    pub fn all(self) -> bool {
        unsafe { _mm256_movemask_epi8(_mm256_cmpeq_epi32(self.0, _mm256_setzero_si256())) == 0 }
    }

    pub fn any(self) -> bool {
        unsafe { _mm256_movemask_epi8(_mm256_cmpeq_epi32(self.0, _mm256_setzero_si256())) != -1 }
    }
}

impl Not for m32x8 {
    type Output = Self;

    fn not(self) -> Self::Output {
        Self(unsafe { _mm256_xor_si256(self.0, _mm256_cmpeq_epi32(self.0, self.0)) })
    }
}

impl BitOr for m32x8 {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm256_or_si256(self.0, rhs.0) })
    }
}

impl BitOrAssign for m32x8 {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = *self | rhs;
    }
}

impl BitXor for m32x8 {
    type Output = Self;

    fn bitxor(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm256_xor_si256(self.0, rhs.0) })
    }
}

impl Default for m32x8 {
    fn default() -> Self {
        Self(unsafe { _mm256_setzero_si256() })
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct u8x8(i64);

impl From<f32x8> for u8x8 {
    fn from(val: f32x8) -> Self {
        unsafe {
            let _i32x8 = _mm256_cvtps_epi32(val.0);
            let _i16x16 = _mm256_packs_epi32(_i32x8, _mm256_setzero_si256());
            let _i16x16 =
                _mm256_permutevar8x32_epi32(_i16x16, _mm256_setr_epi32(0, 1, 4, 5, 0, 0, 0, 0));
            let _i16x8 = _mm256_castsi256_si128(_i16x16);
            let _u8x16 = _mm_packus_epi16(_i16x8, _mm_setzero_si128());

            Self(_mm_cvtsi128_si64x(_u8x16))
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct u8x32(__m256i);

impl u8x32 {
    pub fn splat(val: u8) -> Self {
        Self(unsafe { _mm256_set1_epi8(i8::from_ne_bytes(val.to_ne_bytes())) })
    }

    pub fn swizzle<
        const I0: usize,
        const I1: usize,
        const I2: usize,
        const I3: usize,
        const I4: usize,
        const I5: usize,
        const I6: usize,
        const I7: usize,
        const I8: usize,
        const I9: usize,
        const I10: usize,
        const I11: usize,
        const I12: usize,
        const I13: usize,
        const I14: usize,
        const I15: usize,
        const I16: usize,
        const I17: usize,
        const I18: usize,
        const I19: usize,
        const I20: usize,
        const I21: usize,
        const I22: usize,
        const I23: usize,
        const I24: usize,
        const I25: usize,
        const I26: usize,
        const I27: usize,
        const I28: usize,
        const I29: usize,
        const I30: usize,
        const I31: usize,
    >(
        self,
    ) -> Self {
        Self(unsafe {
            let shuffle = _mm256_set_epi8(
                I31 as i8,
                I30 as i8,
                I29 as i8,
                I28 as i8,
                I27 as i8,
                I26 as i8,
                I25 as i8,
                I24 as i8,
                I23 as i8,
                I22 as i8,
                I21 as i8,
                I20 as i8,
                I19 as i8,
                I18 as i8,
                I17 as i8,
                I16 as i8,
                I15 as i8 ^ 0b1_0000,
                I14 as i8 ^ 0b1_0000,
                I13 as i8 ^ 0b1_0000,
                I12 as i8 ^ 0b1_0000,
                I11 as i8 ^ 0b1_0000,
                I10 as i8 ^ 0b1_0000,
                I9 as i8 ^ 0b1_0000,
                I8 as i8 ^ 0b1_0000,
                I7 as i8 ^ 0b1_0000,
                I6 as i8 ^ 0b1_0000,
                I5 as i8 ^ 0b1_0000,
                I4 as i8 ^ 0b1_0000,
                I3 as i8 ^ 0b1_0000,
                I2 as i8 ^ 0b1_0000,
                I1 as i8 ^ 0b1_0000,
                I0 as i8 ^ 0b1_0000,
            );

            let shuffle_lo = _mm256_shuffle_epi8(self.0, shuffle);

            let mut _i16x8_lo = _mm_undefined_si128();
            let mut _i16x8_hi = _mm_undefined_si128();
            _mm256_storeu2_m128i(
                ptr::addr_of_mut!(_i16x8_hi),
                ptr::addr_of_mut!(_i16x8_lo),
                self.0,
            );
            let swapped = _mm256_loadu2_m128i(&_i16x8_lo, &_i16x8_hi);

            let shuffle_hi = _mm256_shuffle_epi8(swapped, shuffle);
            let mask = _mm256_cmpgt_epi8(shuffle, _mm256_set1_epi8(15));

            _mm256_blendv_epi8(shuffle_hi, shuffle_lo, mask)
        })
    }
}

impl Default for u8x32 {
    fn default() -> Self {
        Self(unsafe { _mm256_setzero_si256() })
    }
}

#[derive(Clone, Copy, Debug)]
pub struct i8x16(__m128i);

impl i8x16 {
    #[cfg(test)]
    pub fn as_mut_array(&mut self) -> &mut [i8; 16] {
        unsafe { std::mem::transmute(&mut self.0) }
    }

    pub fn splat(val: i8) -> Self {
        Self(unsafe { _mm_set1_epi8(val) })
    }

    pub fn eq(self, other: Self) -> m8x16 {
        m8x16(unsafe { _mm_cmpeq_epi8(self.0, other.0) })
    }

    pub fn abs(self) -> Self {
        Self(unsafe { _mm_abs_epi8(self.0) })
    }
}

impl Default for i8x16 {
    fn default() -> Self {
        Self(unsafe { _mm_setzero_si128() })
    }
}

impl Add for i8x16 {
    type Output = Self;

    fn add(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm_add_epi8(self.0, rhs.0) })
    }
}

impl AddAssign for i8x16 {
    fn add_assign(&mut self, rhs: Self) {
        *self = *self + rhs;
    }
}

impl BitAnd for i8x16 {
    type Output = Self;

    fn bitand(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm_and_si128(self.0, rhs.0) })
    }
}

impl Into<[i32x8; 2]> for i8x16 {
    fn into(self) -> [i32x8; 2] {
        unsafe {
            let _i8x16_lo = _mm_unpacklo_epi64(self.0, _mm_setzero_si128());
            let _i8x16_hi = _mm_unpackhi_epi64(self.0, _mm_setzero_si128());

            [i32x8(_mm256_cvtepi8_epi32(_i8x16_lo)), i32x8(_mm256_cvtepi8_epi32(_i8x16_hi))]
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct i16x16(__m256i);

impl i16x16 {
    pub fn splat(val: i16) -> Self {
        Self(unsafe { _mm256_set1_epi16(val) })
    }
}

impl Default for i16x16 {
    fn default() -> Self {
        Self(unsafe { _mm256_setzero_si256() })
    }
}

impl Into<[i32x8; 2]> for i16x16 {
    fn into(self) -> [i32x8; 2] {
        unsafe {
            let mut _i16x8_lo = _mm_undefined_si128();
            let mut _i16x8_hi = _mm_undefined_si128();

            _mm256_storeu2_m128i(
                ptr::addr_of_mut!(_i16x8_hi),
                ptr::addr_of_mut!(_i16x8_lo),
                self.0,
            );

            [i32x8(_mm256_cvtepi16_epi32(_i16x8_lo)), i32x8(_mm256_cvtepi16_epi32(_i16x8_hi))]
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct i32x8(__m256i);

impl i32x8 {
    pub fn splat(val: i32) -> Self {
        Self(unsafe { _mm256_set1_epi32(val) })
    }

    pub fn eq(self, other: Self) -> m32x8 {
        m32x8(unsafe { _mm256_cmpeq_epi32(self.0, other.0) })
    }
}

impl Default for i32x8 {
    fn default() -> Self {
        Self(unsafe { _mm256_setzero_si256() })
    }
}

impl Add for i32x8 {
    type Output = Self;

    fn add(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm256_add_epi32(self.0, rhs.0) })
    }
}

impl Mul for i32x8 {
    type Output = Self;

    fn mul(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm256_mullo_epi32(self.0, rhs.0) })
    }
}

impl BitAnd for i32x8 {
    type Output = Self;

    fn bitand(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm256_and_si256(self.0, rhs.0) })
    }
}

impl Shr for i32x8 {
    type Output = Self;

    fn shr(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm256_srav_epi32(self.0, rhs.0) })
    }
}

#[derive(Clone, Copy, Debug)]
pub struct f32x8(__m256);

impl f32x8 {
    pub fn splat(val: f32) -> Self {
        Self(unsafe { _mm256_set1_ps(val) })
    }

    pub fn indexed() -> Self {
        Self(unsafe { _mm256_set_ps(7.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0, 0.0) })
    }

    #[cfg(test)]
    pub fn as_array(&self) -> &[f32; 8] {
        unsafe { std::mem::transmute(&self.0) }
    }

    pub fn eq(self, other: Self) -> m32x8 {
        m32x8(unsafe { _mm256_castps_si256(_mm256_cmp_ps(self.0, other.0, _CMP_EQ_OQ)) })
    }

    pub fn lt(self, other: Self) -> m32x8 {
        m32x8(unsafe { _mm256_castps_si256(_mm256_cmp_ps(self.0, other.0, _CMP_LT_OQ)) })
    }

    pub fn le(self, other: Self) -> m32x8 {
        m32x8(unsafe { _mm256_castps_si256(_mm256_cmp_ps(self.0, other.0, _CMP_LE_OQ)) })
    }

    pub fn select(self, other: Self, mask: m32x8) -> Self {
        Self(unsafe { _mm256_blendv_ps(other.0, self.0, _mm256_castsi256_ps(mask.0)) })
    }

    pub fn abs(self) -> Self {
        Self(unsafe { _mm256_andnot_ps(_mm256_set1_ps(-0.0), self.0) })
    }

    pub fn min(self, other: Self) -> Self {
        let mask = self.le(other);
        self.select(other, mask)
    }

    pub fn max(self, other: Self) -> Self {
        let mask = other.le(self);
        self.select(other, mask)
    }

    pub fn clamp(self, min: Self, max: Self) -> Self {
        self.min(max).max(min)
    }

    pub fn sqrt(self) -> Self {
        Self(unsafe { _mm256_sqrt_ps(self.0) })
    }

    pub fn mul_add(self, a: Self, b: Self) -> Self {
        Self(unsafe { _mm256_fmadd_ps(self.0, a.0, b.0) })
    }
}

impl Default for f32x8 {
    fn default() -> Self {
        Self(unsafe { _mm256_setzero_ps() })
    }
}

impl Add for f32x8 {
    type Output = Self;

    fn add(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm256_add_ps(self.0, rhs.0) })
    }
}

impl AddAssign for f32x8 {
    fn add_assign(&mut self, rhs: Self) {
        *self = *self + rhs;
    }
}

impl Sub for f32x8 {
    type Output = Self;

    fn sub(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm256_sub_ps(self.0, rhs.0) })
    }
}

impl Mul for f32x8 {
    type Output = Self;

    fn mul(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm256_mul_ps(self.0, rhs.0) })
    }
}

impl MulAssign for f32x8 {
    fn mul_assign(&mut self, rhs: Self) {
        *self = *self * rhs;
    }
}

impl Div for f32x8 {
    type Output = Self;

    fn div(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm256_div_ps(self.0, rhs.0) })
    }
}

impl Neg for f32x8 {
    type Output = Self;

    fn neg(self) -> Self::Output {
        Self(unsafe { _mm256_xor_ps(self.0, _mm256_set1_ps(-0.0)) })
    }
}

impl BitOr for f32x8 {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self(unsafe { _mm256_or_ps(self.0, rhs.0) })
    }
}

impl BitOrAssign for f32x8 {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = *self | rhs;
    }
}

impl From<i32x8> for f32x8 {
    fn from(val: i32x8) -> Self {
        Self(unsafe { _mm256_cvtepi32_ps(val.0) })
    }
}
