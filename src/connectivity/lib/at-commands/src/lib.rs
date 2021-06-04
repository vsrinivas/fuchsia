// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Export all the lowlevel types in one module to simplify generated code.
mod lowlevel {
    mod arguments;
    mod command;
    mod response;

    pub mod write_to;

    pub use arguments::{Argument, Arguments, DelimitedArguments};
    pub use command::Command;
    pub use response::{HardcodedError, Response};
}

mod generated {
    pub mod translate;
    pub mod types;
}

// Reexport all the highlevel types in one module to simplify generated code.
mod highlevel {
    pub use crate::generated::types::{Command, Success};
    pub use crate::response::{HardcodedError, Response};
}

mod translate {
    pub use crate::generated::translate::{lower_command, raise_command};
    pub use crate::translate_response::{lower_response, raise_response};
}

mod parser {
    mod arguments_parser;
    pub mod command_grammar;
    pub mod command_parser;
    pub mod common;
    pub mod response_grammar;
    pub mod response_parser;
}

mod response;
mod serde;
mod translate_response;
mod translate_util;

// Reexport generated high level types and functions for use by clients.
pub use generated::translate::*;
pub use generated::types::*;
pub use response::*;
pub use serde::*;
