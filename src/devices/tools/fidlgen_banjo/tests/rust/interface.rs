// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.interface banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon_types as zircon_types;


#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct CookieJarArgs {
    pub name: [u8; 100 as usize],
}


#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct CookieKind(pub u32);

impl CookieKind {
    pub const CHOCOLATE: Self = Self(0);
    pub const GINGERBREAD: Self = Self(1);
    pub const SNICKERDOODLE: Self = Self(2);
}

impl std::ops::BitAnd for CookieKind {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for CookieKind {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for CookieKind {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for CookieKind {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for CookieKind {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for CookieKind {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union ChangeArgs {
    pub intf: cookie_maker,
    pub jarrer: cookie_jarrer,
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for ChangeArgs {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<ChangeArgs>")
    }
}


