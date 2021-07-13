// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This crate doesn't comply with all 2018 idioms
#![allow(elided_lifetimes_in_paths)]

pub mod bytecode_constants;
pub mod bytecode_encoder;
pub mod compiler;
pub mod ddk_bind_constants;
pub mod debugger;
mod errors;
pub mod interpreter;
pub mod linter;
pub mod parser;
pub mod test;
