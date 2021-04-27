// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.rustderive banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;


pub const SMALL_ARRAY_SIZE: u32 = 8;
pub const LARGE_ARRAY_SIZE: u32 = 2048;
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct UnionParentStruct {
    pub some_union: SomeUnion,
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct UnionGrandParentStruct {
    pub field: UnionParentStruct,
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct Baz2 {
    pub foo: *mut Foo2,
    pub bar: *mut Bar2,
    pub baz: *mut Baz2,
    pub some_union: SomeUnion,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct EnumParentStruct {
    pub some_enum: SomeEnum,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct EnumGrandParentStruct {
    pub field: EnumParentStruct,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct SmallArrayStruct {
    pub small_array: [u8; 8 as usize],
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct BigArrayStruct {
    pub big_array: [u8; 2048 as usize],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct Baz {
    pub foo: *mut Foo,
    pub bar: *mut Bar,
    pub baz: *mut Baz,
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct Bar2 {
    pub bar: *mut Foo2,
    pub baz: *mut Baz2,
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct Foo2 {
    pub bar: Bar2,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct Bar {
    pub bar: *mut Foo,
    pub baz: *mut Baz,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct Foo {
    pub bar: Bar,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct SomeEnum(pub u32);

impl SomeEnum {
    pub const V1: Self = Self(1);
    pub const V2: Self = Self(2);
}

impl std::ops::BitAnd for SomeEnum {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for SomeEnum {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for SomeEnum {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for SomeEnum {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for SomeEnum {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for SomeEnum {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union SomeUnion {
    pub bytes1: [u8; 8 as usize],
    pub bytes2: [u8; 16 as usize],
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for SomeUnion {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<SomeUnion>")
    }
}

