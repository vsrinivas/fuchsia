// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Link all modules needed for tests for the AT command library.
mod lowlevel {
    pub mod arguments;
    pub mod command;
    pub mod response;
    pub mod write_to;

    // Make these available in lowlevel to simplify generated code.
    pub use arguments::{Argument, Arguments, PrimitiveArgument};
    pub use command::{Command, ExecuteArguments};
    pub use response::{HardcodedError, Response};

    // Tests
    mod command_tests;
    mod response_tests;
}

mod parser {
    mod arguments_parser;
    mod command_grammar;
    pub mod command_parser;
    pub mod common;
    mod response_grammar;
    pub mod response_parser;

    // Tests
    mod command_parser_tests;
    mod response_parser_tests;
}

mod generated;
mod serde;
mod translate_util;

// Tests
mod command_generated_tests;
mod response_generated_tests;
