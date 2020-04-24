// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]

use std::{
    mem,
    ops::{Index, IndexMut, Mul, MulAssign, Sub},
};

#[repr(transparent)]
#[derive(Clone, Copy, Debug)]
pub struct m32x8 {
    array: [u32; 8],
}

#[repr(transparent)]
#[derive(Clone, Copy, Debug)]
pub struct u8x8 {
    array: [u8; 8],
}

impl u8x8 {
    #[inline]
    pub const fn splat(value: u8) -> Self {
        Self { array: [value; 8] }
    }
}

#[repr(transparent)]
#[derive(Clone, Copy, Debug)]
pub struct f32x4 {
    array: [f32; 4],
}

impl f32x4 {
    #[inline]
    pub const fn from_array(array: [f32; 4]) -> Self {
        Self { array }
    }

    #[inline]
    pub const fn splat(value: f32) -> Self {
        Self { array: [value; 4] }
    }
}

impl Index<usize> for f32x4 {
    type Output = f32;

    fn index(&self, index: usize) -> &Self::Output {
        &self.array[index]
    }
}

impl IndexMut<usize> for f32x4 {
    fn index_mut(&mut self, index: usize) -> &mut Self::Output {
        &mut self.array[index]
    }
}

impl Mul for f32x4 {
    type Output = Self;

    #[inline]
    fn mul(mut self, rhs: Self) -> Self::Output {
        for (l, &r) in self.array.iter_mut().zip(rhs.array.iter()) {
            *l *= r;
        }

        self
    }
}

impl MulAssign for f32x4 {
    #[inline]
    fn mul_assign(&mut self, rhs: Self) {
        *self = *self * rhs;
    }
}

#[repr(transparent)]
#[derive(Clone, Copy, Debug)]
pub struct f32x8 {
    array: [f32; 8],
}

impl f32x8 {
    #[inline]
    pub const fn from_array(array: [f32; 8]) -> Self {
        Self { array }
    }

    #[inline]
    pub const fn splat(value: f32) -> Self {
        Self { array: [value; 8] }
    }

    #[inline]
    pub fn select(mask: m32x8, mut a: Self, b: Self) -> Self {
        for (&m, (a, b)) in mask.array.iter().zip(a.array.iter_mut().zip(b.array.iter())) {
            *a = if m != 0 { *a } else { *b };
        }

        a
    }

    #[inline]
    pub fn recip(mut self) -> Self {
        for v in self.array.iter_mut() {
            *v = v.recip();
        }

        self
    }

    #[inline]
    pub fn sqrt(mut self) -> Self {
        for v in self.array.iter_mut() {
            *v = v.sqrt();
        }

        self
    }

    #[inline]
    pub fn mul_add(mut self, a: Self, b: Self) -> Self {
        for (v, (&a, &b)) in self.array.iter_mut().zip(a.array.iter().zip(b.array.iter())) {
            *v = v.mul_add(a, b);
        }

        self
    }

    #[inline]
    pub fn le(mut self, other: Self) -> m32x8 {
        for (l, &r) in self.array.iter_mut().zip(other.array.iter()) {
            *l = if *l <= r { f32::from_bits(0xFFFFFFFF) } else { f32::from_bits(0x00000000) };
        }

        unsafe { mem::transmute(self) }
    }
}

impl Index<usize> for f32x8 {
    type Output = f32;

    fn index(&self, index: usize) -> &Self::Output {
        &self.array[index]
    }
}

impl Sub for f32x8 {
    type Output = Self;

    fn sub(mut self, rhs: Self) -> Self::Output {
        for (l, &r) in self.array.iter_mut().zip(rhs.array.iter()) {
            *l -= r;
        }

        self
    }
}

impl Mul for f32x8 {
    type Output = Self;

    #[inline]
    fn mul(mut self, rhs: Self) -> Self::Output {
        for (l, &r) in self.array.iter_mut().zip(rhs.array.iter()) {
            *l *= r;
        }

        self
    }
}

impl MulAssign for f32x8 {
    #[inline]
    fn mul_assign(&mut self, rhs: Self) {
        *self = *self * rhs;
    }
}

impl Into<u8x8> for f32x8 {
    fn into(self) -> u8x8 {
        u8x8 {
            array: [
                self.array[0] as u8,
                self.array[1] as u8,
                self.array[2] as u8,
                self.array[3] as u8,
                self.array[4] as u8,
                self.array[5] as u8,
                self.array[6] as u8,
                self.array[7] as u8,
            ],
        }
    }
}

impl From<[f32x4; 2]> for f32x8 {
    fn from(values: [f32x4; 2]) -> Self {
        unsafe { mem::transmute(values) }
    }
}

impl Into<[f32x4; 2]> for f32x8 {
    fn into(self) -> [f32x4; 2] {
        unsafe { mem::transmute(self) }
    }
}
