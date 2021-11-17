// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolothertypes banjo file

#![allow(unused_imports, non_camel_case_types)]



pub const STRINGS_SIZE: u32 = 32;
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct ThisIsAStruct {
    pub s: *const std::os::raw::c_char,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct ThisIsAnEnum(pub u32);

impl ThisIsAnEnum {
    pub const X: Self = Self(23);
}

impl std::ops::BitAnd for ThisIsAnEnum {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for ThisIsAnEnum {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for ThisIsAnEnum {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for ThisIsAnEnum {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for ThisIsAnEnum {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for ThisIsAnEnum {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union ThisIsAUnion {
    pub s: *const std::os::raw::c_char,
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for ThisIsAUnion {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<ThisIsAUnion>")
    }
}

