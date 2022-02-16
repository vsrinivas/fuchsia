// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.attributes banjo file

#![allow(unused_imports, non_camel_case_types)]




#[repr(C, packed)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct packed_struct {
    pub foo: i32,
    pub bar: i32,
    pub baz: i32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct none_struct {
    pub foo: i32,
    pub bar: i32,
    pub baz: i32,
}



#[repr(C, packed)]
#[derive(Copy, Clone)]
pub union packed_union {
    pub foo: i32,
    pub bar: i32,
    pub baz: i32,
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for packed_union {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<packed_union>")
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union none_union {
    pub foo: i32,
    pub bar: i32,
    pub baz: i32,
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for none_union {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<none_union>")
    }
}

