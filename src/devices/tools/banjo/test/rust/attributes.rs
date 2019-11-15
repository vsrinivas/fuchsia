// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.attributes banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;



#[repr(C)]
#[derive(Copy, Clone)]
pub struct none_struct {
    pub foo: i32,
    pub bar: i32,
    pub baz: i32,
}

#[repr(packed)]
#[derive(Copy, Clone)]
pub struct packed_struct {
    pub foo: i32,
    pub bar: i32,
    pub baz: i32,
}


#[repr(C)]
#[derive(Copy, Clone)]
pub union none_union {
    pub foo: i32,
    pub bar: i32,
    pub baz: i32,
}

#[repr(packed)]
#[derive(Copy, Clone)]
pub union packed_union {
    pub foo: i32,
    pub bar: i32,
    pub baz: i32,
}

