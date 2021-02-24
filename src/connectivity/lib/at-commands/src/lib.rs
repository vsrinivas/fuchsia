// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod lowlevel {
    pub mod arguments;
    pub mod command;
    pub mod response;
    pub mod write_to;

    pub use command::Command;
    pub use response::Response;
}

mod parser {
    mod arguments_parser;
    mod command_grammar;
    pub mod command_parser;
    pub mod common;
    mod response_grammar;
    pub mod response_parser;
}

mod generated;
mod serde;

// Reexport generated high level types and functions for use by clients.
pub use generated::types::*;
pub use serde::*;
