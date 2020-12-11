// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Link all modules needed for tests for the AT command library.

mod lowlevel {
    pub(crate) mod arguments;
    pub(crate) mod command;
    pub(crate) mod response;
}

mod parser {
    mod arguments_parser;
    mod command_grammar;
    mod command_parser;
    mod command_parser_tests;
    mod common;
    mod response_grammar;
    mod response_parser;
    mod response_parser_tests;
}
