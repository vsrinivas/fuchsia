// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.specialized.syscalls banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;




#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct zx_create_options(pub u32);

impl zx_create_options {
    pub const VARIANT0: Self = Self(0);
    pub const VARIANT1: Self = Self(1);
    pub const VARIANT2: Self = Self(2);
}

impl std::ops::BitAnd for zx_create_options {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for zx_create_options {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for zx_create_options {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for zx_create_options {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for zx_create_options {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for zx_create_options {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union zx_create_types {
    pub type0: [i8; 1 as usize],
    pub type1: [i16; 1 as usize],
    pub type2: [i32; 1 as usize],
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for zx_create_types {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<zx_create_types>")
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union zx_handle_types {
    pub type0: zircon::sys::zx_handle_t,
    pub type1: zircon::sys::zx_handle_t,
    pub type2: zircon::sys::zx_handle_t,
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for zx_handle_types {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<zx_handle_types>")
    }
}

