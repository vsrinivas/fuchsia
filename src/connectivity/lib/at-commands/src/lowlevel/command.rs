// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module contains an AST for AT commands.
/// The format of of these is not specifed in any one place in the spec, but they are
/// described thoughout HFP 1.8.
use crate::lowlevel::arguments;

/// A single AT command.
#[derive(Debug, Clone, PartialEq)]
pub enum Command {
    /// A command for executing some procedure or setting state.  They may have arguments.
    /// For example `AT+EXAMPLE=1,2,3`.
    Execute { name: String, is_extension: bool, arguments: Option<ExecuteArguments> },
    /// A command for reading some state.  For example `AT+EXAMPLE?`.
    Read { name: String, is_extension: bool },
    /// A command for querying the capabilities of the remote side.  For example `AT+EXAMPLE=?`.
    Test { name: String, is_extension: bool },
}

/// Arguments to an execute command.
#[derive(Debug, Clone, PartialEq)]
pub struct ExecuteArguments {
    /// A character setting off arguments from the command with something other than `=`.
    /// This is currently only `>` in the `ATD>n` command, specified in HFP v1.8 4.19.
    pub nonstandard_delimiter: Option<String>,
    /// The actual arguments to the execute commmand.
    pub arguments: arguments::Arguments,
}
