// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.tables banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon_types as zircon_types;


#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct F {
    pub quuz: E,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct C {
    pub baz: zircon_types::zx_handle_t,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct D {
    pub qux: C,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct B {
    pub bar: *mut A,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct A {
    pub foo: *mut B,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct H {
    pub flags: G,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct E {
    pub quux: u8,
}


#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct G(pub u32);

impl G {
    pub const ONLINE: Self = Self(0x01);
}

impl std::ops::BitAnd for G {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for G {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for G {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for G {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for G {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for G {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}


