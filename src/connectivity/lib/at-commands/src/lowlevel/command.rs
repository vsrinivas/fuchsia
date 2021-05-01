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
    Execute { name: String, is_extension: bool, arguments: arguments::DelimitedArguments },
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
            Command::Execute { arguments: args, .. } => args.write_to(sink)?,
            Command::Read { .. } => sink.write_all(b"?")?,
            Command::Test { .. } => sink.write_all(b"=?")?,
        };
        // Commands are terminated by CR.
        sink.write_all(b"\r")
    }
}
