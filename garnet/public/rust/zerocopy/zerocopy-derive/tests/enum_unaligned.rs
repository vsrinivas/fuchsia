// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(repr_align_enum)]
#![allow(warnings)]

use zerocopy::Unaligned;

// An enum is Unaligned if:
// - No repr(align(N > 1))
// - repr(u8) or repr(i8)

#[derive(Unaligned)]
#[repr(u8)]
enum Foo {
    A,
}

#[derive(Unaligned)]
#[repr(i8)]
enum Bar {
    A,
}

#[derive(Unaligned)]
#[repr(u8, align(1))]
enum Baz {
    A,
}

#[derive(Unaligned)]
#[repr(i8, align(1))]
enum Blah {
    B,
}
