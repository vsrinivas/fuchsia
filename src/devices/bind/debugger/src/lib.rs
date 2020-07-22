// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This crate doesn't comply with all 2018 idioms
#![allow(elided_lifetimes_in_paths)]

mod bind_library;
mod bind_program;
mod c_generation;
pub mod compiler;
pub mod debugger;
mod dependency_graph;
mod device_specification;
mod errors;
pub mod instruction;
pub mod offline_debugger;
mod parser_common;
pub mod test;
