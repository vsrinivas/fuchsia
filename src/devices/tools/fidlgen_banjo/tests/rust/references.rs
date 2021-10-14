// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.references banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon as zircon;



#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct SomeType {
    pub value: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct VectorFieldInStruct {
    pub the_vector_list: *const *mut SomeType,
    pub the_vector_count: usize,
    pub the_other_vector_list: *const *mut SomeType,
    pub the_other_vector_count: usize,
    pub the_mutable_vector_list: *mut SomeType,
    pub the_mutable_vector_count: usize,
    pub the_mutable_vector_of_boxes_list: *mut *mut SomeType,
    pub the_mutable_vector_of_boxes_count: usize,
    pub the_default_vector_list: *const SomeType,
    pub the_default_vector_count: usize,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct MutableField {
    pub some_string: *mut std::ffi::c_void /* String */,
    pub some_other_string: *mut std::ffi::c_void /* String */,
    pub some_default_string: *mut std::ffi::c_void /* String */,
}



