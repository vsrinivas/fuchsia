// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.example9 banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon_types as zircon_types;

pub const FAVORITE_ECHO: EchoMe = EchoMe.zero;
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct Echo_EchoEmpty_Response {

}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct EchoMore {
    pub first: u32,
    pub second: u64,
}


#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct EchoMe(pub u32);

impl EchoMe {
    pub const ZERO: Self = Self(0);
    pub const ONE: Self = Self(1);
}

impl std::ops::BitAnd for EchoMe {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for EchoMe {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for EchoMe {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for EchoMe {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for EchoMe {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for EchoMe {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}


#[repr(C)]
#[derive(Copy, Clone)]
pub union Echo_EchoEmpty_Result {
    pub response: Echo_EchoEmpty_Response,
    pub err: u32,
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for Echo_EchoEmpty_Result {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<Echo_EchoEmpty_Result>")
    }
}

