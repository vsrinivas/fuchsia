// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.flag banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;




#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct Topic(pub u32);

impl Topic {
    pub const TOPIC0: Self = Self(0);
    pub const TOPIC1: Self = Self(1);
    pub const TOPIC2: Self = Self(2);
    pub const TOPIC3: Self = Self(3);
    pub const TOPIC4: Self = Self(4);
    pub const TOPIC5: Self = Self(5);
}

impl std::ops::BitAnd for Topic {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for Topic {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for Topic {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for Topic {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for Topic {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for Topic {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}


