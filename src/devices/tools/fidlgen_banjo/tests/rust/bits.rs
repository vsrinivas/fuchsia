// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.bits banjo file

#![allow(unused_imports, non_camel_case_types)]







#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct uint8_bits(pub u8);

impl uint8_bits {
    pub const KONE: Self = Self(1);
    pub const KTWO: Self = Self(2);
    pub const KFOUR: Self = Self(4);
    pub const KEIGHT: Self = Self(8);
    pub const KSIXTEEN: Self = Self(16);
}

impl std::ops::BitAnd for uint8_bits {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for uint8_bits {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for uint8_bits {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for uint8_bits {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for uint8_bits {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for uint8_bits {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct uint64_bits(pub u64);

impl uint64_bits {
    pub const KONE: Self = Self(1);
    pub const KTWO: Self = Self(2);
    pub const KFOUR: Self = Self(4);
    pub const KEIGHT: Self = Self(8);
    pub const KSIXTEEN: Self = Self(16);
    pub const KTHIRTYTWO: Self = Self(32);
    pub const KSIXTYFOUR: Self = Self(64);
    pub const KONEHUNDREDTWENTYEIGHT: Self = Self(128);
}

impl std::ops::BitAnd for uint64_bits {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for uint64_bits {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for uint64_bits {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for uint64_bits {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for uint64_bits {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for uint64_bits {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct uint32_bits(pub u32);

impl uint32_bits {
    pub const KONE: Self = Self(1);
    pub const KTWO: Self = Self(2);
    pub const KFOUR: Self = Self(4);
    pub const KEIGHT: Self = Self(8);
    pub const KSIXTEEN: Self = Self(16);
    pub const KTHIRTYTWO: Self = Self(32);
    pub const KSIXTYFOUR: Self = Self(64);
}

impl std::ops::BitAnd for uint32_bits {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for uint32_bits {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for uint32_bits {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for uint32_bits {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for uint32_bits {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for uint32_bits {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct uint16_bits(pub u16);

impl uint16_bits {
    pub const KONE: Self = Self(1);
    pub const KTWO: Self = Self(2);
    pub const KFOUR: Self = Self(4);
    pub const KEIGHT: Self = Self(8);
    pub const KSIXTEEN: Self = Self(16);
    pub const KTHIRTYTWO: Self = Self(32);
}

impl std::ops::BitAnd for uint16_bits {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for uint16_bits {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for uint16_bits {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for uint16_bits {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for uint16_bits {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for uint16_bits {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}


