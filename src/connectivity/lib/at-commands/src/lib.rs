// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Build lowlevel types into the library but don't let clients use them.

mod lowlevel {
    pub(crate) mod arguments;
    pub(crate) mod command;
    pub(crate) mod response;
    mod write_to;
}

mod parser {
    mod arguments_parser;
    mod command_grammar;
    pub(crate) mod command_parser;
    mod common;
    mod response_grammar;
    pub(crate) mod response_parser;
}

// Reexport generated high level types for use by clients.
mod generated;
pub use generated::*;
