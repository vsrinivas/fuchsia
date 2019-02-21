// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(warnings)]

use zerocopy::Unaligned;
use zerocopy_derive::Unaligned;

// A struct is Unaligned if:
// - repr(align) is no more than 1 and either
//   - repr(C) or repr(transparent) and
//     - all fields Unaligned
//   - repr(packed)

#[derive(Unaligned)]
#[repr(C)]
struct Foo {
    a: u8,
}

#[derive(Unaligned)]
#[repr(transparent)]
struct Bar {
    a: u8,
}

#[derive(Unaligned)]
#[repr(packed)]
struct Baz {
    a: u16,
}

#[derive(Unaligned)]
#[repr(C, align(1))]
struct FooAlign {
    a: u8,
}
