// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(repr_align_enum)]
#![allow(warnings)]

use zerocopy::AsBytes;

// An enum is AsBytes if if has a defined repr

#[derive(AsBytes)]
#[repr(C)]
enum C {
    A,
}

#[derive(AsBytes)]
#[repr(u8)]
enum U8 {
    A,
}

#[derive(AsBytes)]
#[repr(u16)]
enum U16 {
    A,
}

#[derive(AsBytes)]
#[repr(u32)]
enum U32 {
    A,
}

#[derive(AsBytes)]
#[repr(u64)]
enum U64 {
    A,
}

#[derive(AsBytes)]
#[repr(usize)]
enum Usize {
    A,
}

#[derive(AsBytes)]
#[repr(i8)]
enum I8 {
    A,
}

#[derive(AsBytes)]
#[repr(i16)]
enum I16 {
    A,
}

#[derive(AsBytes)]
#[repr(i32)]
enum I32 {
    A,
}

#[derive(AsBytes)]
#[repr(i64)]
enum I64 {
    A,
}

#[derive(AsBytes)]
#[repr(isize)]
enum Isize {
    A,
}
