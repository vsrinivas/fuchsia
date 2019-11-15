// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.enums banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;




#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct int8_enum(i8);

impl int8_enum {
    pub const KNEGATIVEONE: Self = Self(-1);
    pub const KONE: Self = Self(1);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct int16_enum(i16);

impl int16_enum {
    pub const KNEGATIVEONE: Self = Self(-1);
    pub const KONE: Self = Self(1);
    pub const KTWO: Self = Self(2);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct int32_enum(i32);

impl int32_enum {
    pub const KNEGATIVEONE: Self = Self(-1);
    pub const KONE: Self = Self(1);
    pub const KTWO: Self = Self(2);
    pub const KTHREE: Self = Self(3);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct int64_enum(i64);

impl int64_enum {
    pub const KNEGATIVEONE: Self = Self(-1);
    pub const KONE: Self = Self(1);
    pub const KTWO: Self = Self(2);
    pub const KTHREE: Self = Self(3);
    pub const KFOUR: Self = Self(4);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct uint8_enum(u8);

impl uint8_enum {
    pub const KONE: Self = Self(1);
    pub const KTWO: Self = Self(2);
    pub const KTHREE: Self = Self(3);
    pub const KFOUR: Self = Self(4);
    pub const KFIVE: Self = Self(5);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct uint16_enum(u16);

impl uint16_enum {
    pub const KONE: Self = Self(1);
    pub const KTWO: Self = Self(2);
    pub const KTHREE: Self = Self(3);
    pub const KFOUR: Self = Self(4);
    pub const KFIVE: Self = Self(5);
    pub const KSIX: Self = Self(6);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct uint32_enum(u32);

impl uint32_enum {
    pub const KONE: Self = Self(1);
    pub const KTWO: Self = Self(2);
    pub const KTHREE: Self = Self(3);
    pub const KFOUR: Self = Self(4);
    pub const KFIVE: Self = Self(5);
    pub const KSIX: Self = Self(6);
    pub const KSEVEN: Self = Self(7);
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct uint64_enum(u64);

impl uint64_enum {
    pub const KONE: Self = Self(1);
    pub const KTWO: Self = Self(2);
    pub const KTHREE: Self = Self(3);
    pub const KFOUR: Self = Self(4);
    pub const KFIVE: Self = Self(5);
    pub const KSIX: Self = Self(6);
    pub const KSEVEN: Self = Self(7);
    pub const KEIGHT: Self = Self(8);
}



