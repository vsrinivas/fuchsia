// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.alias banjo file

#![allow(unused_imports, non_camel_case_types)]




#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct SomeStruct {
    pub one: u16,
    pub two: u32,
    pub primitive: u32,
    pub vector_alias_list: *const u8,
    pub vector_alias_count: usize,
    pub array_alias: [u8; 32 as usize],
    pub nested_alias: [[u8; 32 as usize]; 32 as usize],
}



