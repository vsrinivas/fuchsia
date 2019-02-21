// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(warnings)]

use std::marker::PhantomData;
use std::option::IntoIter;

use zerocopy::AsBytes;
use zerocopy_derive::AsBytes;

// A struct is AsBytes if:
// - all fields are AsBytes
// - repr(C) or repr(transparent) and
//   - no padding (size of struct equals sum of size of field types)
// - repr(packed)

#[derive(AsBytes)]
#[repr(C)]
struct CZst;

#[derive(AsBytes)]
#[repr(C)]
struct C {
    a: u8,
    b: u8,
    c: u16,
}

#[derive(AsBytes)]
#[repr(transparent)]
struct Transparent {
    a: u8,
    b: CZst,
}

#[derive(AsBytes)]
#[repr(C, packed)]
struct CZstPacked;

#[derive(AsBytes)]
#[repr(C, packed)]
struct CPacked {
    a: u8,
    b: u16,
}
