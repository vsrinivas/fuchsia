// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains an AST for AT commands.
//!
//! The format of of these is not specifed in any one place in the spec, but they are
//! described thoughout HFP 1.8.

use {
    crate::lowlevel::{arguments, write_to::WriteTo},
    std::io,
};

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

impl Command {
    fn name(&self) -> &str {
        match self {
            Command::Execute { name, .. }
            | Command::Read { name, .. }
            | Command::Test { name, .. } => name,
        }
    }

    fn is_extension(&self) -> bool {
        match self {
            Command::Execute { is_extension, .. }
            | Command::Read { is_extension, .. }
            | Command::Test { is_extension, .. } => *is_extension,
        }
    }
}

impl WriteTo for Command {
    fn write_to<W: io::Write>(&self, sink: &mut W) -> io::Result<()> {
        sink.write_all(b"AT")?;
        if self.is_extension() {
            sink.write_all(b"+")?
        }
        sink.write_all(self.name().as_bytes())?;
        match self {
            Command::Execute { arguments: Some(args), .. } => args.write_to(sink)?,
            Command::Execute { arguments: None, .. } => (),
            Command::Read { .. } => sink.write_all(b"?")?,
            Command::Test { .. } => sink.write_all(b"=?")?,
        };
        // Commands are terminated by CR.
        sink.write_all(b"\r")
    }
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

impl WriteTo for ExecuteArguments {
    fn write_to<W: io::Write>(&self, sink: &mut W) -> io::Result<()> {
        let ExecuteArguments { nonstandard_delimiter, arguments } = self;
        match nonstandard_delimiter {
            Some(string) => sink.write_all(string.as_bytes())?,
            _ => sink.write_all(b"=")?,
        }
        arguments.write_to(sink)
    }
}
