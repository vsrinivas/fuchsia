// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::{
    Add, AddAssign, BitAnd, BitOr, BitOrAssign, BitXor, Div, Mul, MulAssign, Neg, Not, Shr, Sub,
};

#[derive(Clone, Copy, Debug, Default)]
pub struct m8x16([u8; 16]);

impl m8x16 {
    pub fn all(self) -> bool {
        self.0.iter().all(|&val| val == u8::MAX)
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct m32x8([u32; 8]);

impl m32x8 {
    pub fn all(self) -> bool {
        self.0.iter().all(|&val| val == u32::MAX)
    }

    pub fn any(self) -> bool {
        self.0.iter().any(|&val| val == u32::MAX)
    }
}

impl Not for m32x8 {
    type Output = Self;

    fn not(mut self) -> Self::Output {
        self.0.iter_mut().for_each(|t| *t = !*t);
        self
    }
}

impl BitOr for m32x8 {
    type Output = Self;

    fn bitor(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| *t |= o);
        self
    }
}

impl BitOrAssign for m32x8 {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = *self | rhs;
    }
}

impl BitXor for m32x8 {
    type Output = Self;

    fn bitxor(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| *t ^= o);
        self
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct u8x8([u8; 8]);

impl From<f32x8> for u8x8 {
    fn from(val: f32x8) -> Self {
        Self([
            val.0[0].round() as u8,
            val.0[1].round() as u8,
            val.0[2].round() as u8,
            val.0[3].round() as u8,
            val.0[4].round() as u8,
            val.0[5].round() as u8,
            val.0[6].round() as u8,
            val.0[7].round() as u8,
        ])
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct u8x32([u8; 32]);

impl u8x32 {
    pub fn splat(val: u8) -> Self {
        Self([
            val, val, val, val, val, val, val, val, val, val, val, val, val, val, val, val, val,
            val, val, val, val, val, val, val, val, val, val, val, val, val, val, val,
        ])
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
        Self([
            self.0[I0],
            self.0[I1],
            self.0[I2],
            self.0[I3],
            self.0[I4],
            self.0[I5],
            self.0[I6],
            self.0[I7],
            self.0[I8],
            self.0[I9],
            self.0[I10],
            self.0[I11],
            self.0[I12],
            self.0[I13],
            self.0[I14],
            self.0[I15],
            self.0[I16],
            self.0[I17],
            self.0[I18],
            self.0[I19],
            self.0[I20],
            self.0[I21],
            self.0[I22],
            self.0[I23],
            self.0[I24],
            self.0[I25],
            self.0[I26],
            self.0[I27],
            self.0[I28],
            self.0[I29],
            self.0[I30],
            self.0[I31],
        ])
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct i8x16([i8; 16]);

impl i8x16 {
    pub fn splat(val: i8) -> Self {
        Self([val, val, val, val, val, val, val, val, val, val, val, val, val, val, val, val])
    }

    #[cfg(test)]
    pub fn as_mut_array(&mut self) -> &mut [i8; 16] {
        &mut self.0
    }

    pub fn eq(self, other: Self) -> m8x16 {
        m8x16([
            if self.0[0] == other.0[0] { u8::MAX } else { 0 },
            if self.0[1] == other.0[1] { u8::MAX } else { 0 },
            if self.0[2] == other.0[2] { u8::MAX } else { 0 },
            if self.0[3] == other.0[3] { u8::MAX } else { 0 },
            if self.0[4] == other.0[4] { u8::MAX } else { 0 },
            if self.0[5] == other.0[5] { u8::MAX } else { 0 },
            if self.0[6] == other.0[6] { u8::MAX } else { 0 },
            if self.0[7] == other.0[7] { u8::MAX } else { 0 },
            if self.0[8] == other.0[8] { u8::MAX } else { 0 },
            if self.0[9] == other.0[9] { u8::MAX } else { 0 },
            if self.0[10] == other.0[10] { u8::MAX } else { 0 },
            if self.0[11] == other.0[11] { u8::MAX } else { 0 },
            if self.0[12] == other.0[12] { u8::MAX } else { 0 },
            if self.0[13] == other.0[13] { u8::MAX } else { 0 },
            if self.0[14] == other.0[14] { u8::MAX } else { 0 },
            if self.0[15] == other.0[15] { u8::MAX } else { 0 },
        ])
    }

    pub fn abs(mut self) -> Self {
        self.0.iter_mut().for_each(|val| *val = val.abs());
        self
    }
}

impl Add for i8x16 {
    type Output = Self;

    fn add(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| *t += o);
        self
    }
}

impl AddAssign for i8x16 {
    fn add_assign(&mut self, rhs: Self) {
        *self = *self + rhs;
    }
}

impl BitAnd for i8x16 {
    type Output = Self;

    fn bitand(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| *t &= o);
        self
    }
}

impl Into<[i32x8; 2]> for i8x16 {
    fn into(self) -> [i32x8; 2] {
        [
            i32x8([
                self.0[0] as i32,
                self.0[1] as i32,
                self.0[2] as i32,
                self.0[3] as i32,
                self.0[4] as i32,
                self.0[5] as i32,
                self.0[6] as i32,
                self.0[7] as i32,
            ]),
            i32x8([
                self.0[8] as i32,
                self.0[9] as i32,
                self.0[10] as i32,
                self.0[11] as i32,
                self.0[12] as i32,
                self.0[13] as i32,
                self.0[14] as i32,
                self.0[15] as i32,
            ]),
        ]
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct i16x16([i16; 16]);

impl i16x16 {
    pub fn splat(val: i16) -> Self {
        Self([val, val, val, val, val, val, val, val, val, val, val, val, val, val, val, val])
    }
}

impl Into<[i32x8; 2]> for i16x16 {
    fn into(self) -> [i32x8; 2] {
        [
            i32x8([
                self.0[0] as i32,
                self.0[1] as i32,
                self.0[2] as i32,
                self.0[3] as i32,
                self.0[4] as i32,
                self.0[5] as i32,
                self.0[6] as i32,
                self.0[7] as i32,
            ]),
            i32x8([
                self.0[8] as i32,
                self.0[9] as i32,
                self.0[10] as i32,
                self.0[11] as i32,
                self.0[12] as i32,
                self.0[13] as i32,
                self.0[14] as i32,
                self.0[15] as i32,
            ]),
        ]
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct i32x8([i32; 8]);

impl i32x8 {
    pub fn splat(val: i32) -> Self {
        Self([val, val, val, val, val, val, val, val])
    }

    pub fn eq(self, other: Self) -> m32x8 {
        m32x8([
            if self.0[0] == other.0[0] { u32::MAX } else { 0 },
            if self.0[1] == other.0[1] { u32::MAX } else { 0 },
            if self.0[2] == other.0[2] { u32::MAX } else { 0 },
            if self.0[3] == other.0[3] { u32::MAX } else { 0 },
            if self.0[4] == other.0[4] { u32::MAX } else { 0 },
            if self.0[5] == other.0[5] { u32::MAX } else { 0 },
            if self.0[6] == other.0[6] { u32::MAX } else { 0 },
            if self.0[7] == other.0[7] { u32::MAX } else { 0 },
        ])
    }
}

impl Add for i32x8 {
    type Output = Self;

    fn add(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| *t += o);
        self
    }
}

impl Mul for i32x8 {
    type Output = Self;

    fn mul(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| *t *= o);
        self
    }
}

impl BitAnd for i32x8 {
    type Output = Self;

    fn bitand(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| *t &= o);
        self
    }
}

impl Shr for i32x8 {
    type Output = Self;

    fn shr(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| *t >>= o);
        self
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct f32x8([f32; 8]);

impl f32x8 {
    pub fn splat(val: f32) -> Self {
        Self([val, val, val, val, val, val, val, val])
    }

    pub fn indexed() -> Self {
        Self([0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0])
    }

    #[cfg(test)]
    pub fn as_array(&self) -> &[f32; 8] {
        &self.0
    }

    pub fn eq(self, other: Self) -> m32x8 {
        m32x8([
            if self.0[0] == other.0[0] { u32::MAX } else { 0 },
            if self.0[1] == other.0[1] { u32::MAX } else { 0 },
            if self.0[2] == other.0[2] { u32::MAX } else { 0 },
            if self.0[3] == other.0[3] { u32::MAX } else { 0 },
            if self.0[4] == other.0[4] { u32::MAX } else { 0 },
            if self.0[5] == other.0[5] { u32::MAX } else { 0 },
            if self.0[6] == other.0[6] { u32::MAX } else { 0 },
            if self.0[7] == other.0[7] { u32::MAX } else { 0 },
        ])
    }

    pub fn lt(self, other: Self) -> m32x8 {
        m32x8([
            if self.0[0] < other.0[0] { u32::MAX } else { 0 },
            if self.0[1] < other.0[1] { u32::MAX } else { 0 },
            if self.0[2] < other.0[2] { u32::MAX } else { 0 },
            if self.0[3] < other.0[3] { u32::MAX } else { 0 },
            if self.0[4] < other.0[4] { u32::MAX } else { 0 },
            if self.0[5] < other.0[5] { u32::MAX } else { 0 },
            if self.0[6] < other.0[6] { u32::MAX } else { 0 },
            if self.0[7] < other.0[7] { u32::MAX } else { 0 },
        ])
    }

    pub fn le(self, other: Self) -> m32x8 {
        m32x8([
            if self.0[0] <= other.0[0] { u32::MAX } else { 0 },
            if self.0[1] <= other.0[1] { u32::MAX } else { 0 },
            if self.0[2] <= other.0[2] { u32::MAX } else { 0 },
            if self.0[3] <= other.0[3] { u32::MAX } else { 0 },
            if self.0[4] <= other.0[4] { u32::MAX } else { 0 },
            if self.0[5] <= other.0[5] { u32::MAX } else { 0 },
            if self.0[6] <= other.0[6] { u32::MAX } else { 0 },
            if self.0[7] <= other.0[7] { u32::MAX } else { 0 },
        ])
    }

    pub fn select(self, other: Self, mask: m32x8) -> Self {
        Self([
            if mask.0[0] == u32::MAX { self.0[0] } else { other.0[0] },
            if mask.0[1] == u32::MAX { self.0[1] } else { other.0[1] },
            if mask.0[2] == u32::MAX { self.0[2] } else { other.0[2] },
            if mask.0[3] == u32::MAX { self.0[3] } else { other.0[3] },
            if mask.0[4] == u32::MAX { self.0[4] } else { other.0[4] },
            if mask.0[5] == u32::MAX { self.0[5] } else { other.0[5] },
            if mask.0[6] == u32::MAX { self.0[6] } else { other.0[6] },
            if mask.0[7] == u32::MAX { self.0[7] } else { other.0[7] },
        ])
    }

    pub fn abs(mut self) -> Self {
        self.0.iter_mut().for_each(|val| *val = val.abs());
        self
    }

    pub fn min(mut self, other: Self) -> Self {
        self.0.iter_mut().zip(other.0.iter()).for_each(|(t, &o)| *t = t.min(o));
        self
    }

    pub fn max(mut self, other: Self) -> Self {
        self.0.iter_mut().zip(other.0.iter()).for_each(|(t, &o)| *t = t.max(o));
        self
    }

    pub fn clamp(mut self, min: Self, max: Self) -> Self {
        self.0
            .iter_mut()
            .zip(min.0.iter().zip(max.0.iter()))
            .for_each(|(t, (&min, &max))| *t = t.clamp(min, max));
        self
    }

    pub fn sqrt(mut self) -> Self {
        self.0.iter_mut().for_each(|val| *val = val.sqrt());
        self
    }

    pub fn mul_add(mut self, a: Self, b: Self) -> Self {
        self.0
            .iter_mut()
            .zip(a.0.iter().zip(b.0.iter()))
            .for_each(|(t, (&a, &b))| *t = t.mul_add(a, b));
        self
    }
}

impl Add for f32x8 {
    type Output = Self;

    fn add(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| *t += o);
        self
    }
}

impl AddAssign for f32x8 {
    fn add_assign(&mut self, rhs: Self) {
        *self = *self + rhs;
    }
}

impl Sub for f32x8 {
    type Output = Self;

    fn sub(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| *t -= o);
        self
    }
}

impl Mul for f32x8 {
    type Output = Self;

    fn mul(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| *t *= o);
        self
    }
}

impl MulAssign for f32x8 {
    fn mul_assign(&mut self, rhs: Self) {
        *self = *self * rhs;
    }
}

impl Div for f32x8 {
    type Output = Self;

    fn div(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| *t /= o);
        self
    }
}

impl Neg for f32x8 {
    type Output = Self;

    fn neg(mut self) -> Self::Output {
        self.0.iter_mut().for_each(|t| *t = -*t);
        self
    }
}

impl BitOr for f32x8 {
    type Output = Self;

    fn bitor(mut self, rhs: Self) -> Self::Output {
        self.0.iter_mut().zip(rhs.0.iter()).for_each(|(t, &o)| {
            let t_bytes = t.to_ne_bytes();
            let o_bytes = o.to_ne_bytes();

            *t = f32::from_ne_bytes([
                t_bytes[0] | o_bytes[0],
                t_bytes[1] | o_bytes[1],
                t_bytes[2] | o_bytes[2],
                t_bytes[3] | o_bytes[3],
            ]);
        });
        self
    }
}

impl BitOrAssign for f32x8 {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = *self | rhs;
    }
}

impl From<i32x8> for f32x8 {
    fn from(val: i32x8) -> Self {
        Self([
            val.0[0] as f32,
            val.0[1] as f32,
            val.0[2] as f32,
            val.0[3] as f32,
            val.0[4] as f32,
            val.0[5] as f32,
            val.0[6] as f32,
            val.0[7] as f32,
        ])
    }
}
