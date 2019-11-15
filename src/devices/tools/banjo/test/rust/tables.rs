// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.tables banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;



#[repr(C)]
#[derive(Copy, Clone)]
pub struct A {
    pub foo: *mut B,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct B {
    pub bar: *mut A,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct C {
    pub baz: zircon::sys::zx_handle_t,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct D {
    pub qux: C,
}



