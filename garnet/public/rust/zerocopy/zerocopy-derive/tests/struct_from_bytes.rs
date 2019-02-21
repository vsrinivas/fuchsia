// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(warnings)]

use std::marker::PhantomData;
use std::option::IntoIter;

use zerocopy::FromBytes;
use zerocopy_derive::FromBytes;

// A struct is FromBytes if:
// - repr(C) or repr(transparent)
// - all fields are FromBytes

#[derive(FromBytes)]
#[repr(C)]
struct CZst;

#[derive(FromBytes)]
#[repr(C)]
struct C {
    a: u8,
}

#[derive(FromBytes)]
#[repr(transparent)]
struct Transparent {
    a: u8,
    b: CZst,
}

#[derive(FromBytes)]
#[repr(C, packed)]
struct CZstPacked;

#[derive(FromBytes)]
#[repr(C, packed)]
struct CPacked {
    a: u8,
}

#[derive(FromBytes)]
#[repr(C)]
struct TypeParams<'a, T, I: Iterator> {
    a: T,
    c: I::Item,
    d: u8,
    e: PhantomData<&'a [u8]>,
    f: PhantomData<&'static str>,
    g: PhantomData<String>,
}

const _FOO: () = {
    let _: IsFromBytes<TypeParams<'static, (), IntoIter<()>>>;
};

struct IsFromBytes<T: FromBytes>(PhantomData<T>);
