// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Link all modules needed for tests for the AT command library.

/// Export all the lowlevel types in one module to simplify generated code.
mod lowlevel {
    mod arguments;
    mod command;
    mod response;

    pub mod write_to;

    pub use arguments::{Argument, Arguments, DelimitedArguments};
    pub use command::Command;
    pub use response::{HardcodedError, Response};

    // Tests
    mod command_tests;
    mod response_tests;
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

    // Tests
    mod command_parser_tests;
    mod response_parser_tests;
}

mod response;
mod serde;
mod translate_response;
mod translate_util;

// Tests
mod command_generated_tests;
mod response_generated_tests;
mod serde_tests;
