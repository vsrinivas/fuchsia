// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod codegen {
    pub(crate) mod common;
    mod error;
    mod lower;
    mod raise;
    mod toplevel;
    mod types;

    pub use toplevel::codegen;
}
pub mod definition;
pub mod parser;

mod grammar;
