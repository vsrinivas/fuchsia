// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains an AST for arguments for both AT commands and responses.
//!
//! The format of of these is not specifed in any one place in the spec, but they are
//! described thoughout HFP 1.8.

use {crate::lowlevel::write_to::WriteTo, std::io};

/// An argument list set off from a command or response by a delimiter such as "=" or ":".
#[derive(Debug, Clone, PartialEq)]
pub struct DelimitedArguments {
    /// A string setting off arguments from the command or response.  This is normally `=`
    /// or ": ", but could be ">" or absent. This latter is currently only in a variants
    /// of  the `ATD` command, specified in HFP v1.8 4.19.
    pub delimiter: Option<String>,
    /// The actual arguments to the execute commmand.
    pub arguments: Arguments,
    /// An optional terminator for the arguments to the command, such as the ";" used to
    /// terminate ATD commands.
    pub terminator: Option<String>,
}

impl WriteTo for DelimitedArguments {
    fn write_to<W: io::Write>(&self, sink: &mut W) -> io::Result<()> {
        let DelimitedArguments { delimiter, arguments, terminator } = self;
        if let Some(string) = delimiter {
            if string == ":" {
                // Hack.  The parser ignores whitespace, but the colon in a response must be followed by a space, so special case this.
                sink.write_all(": ".as_bytes())?;
            } else {
                sink.write_all(string.as_bytes())?;
            }
        };
        arguments.write_to(sink)?;
        if let Some(terminator) = terminator {
            sink.write_all(terminator.as_bytes())?
        };

        Ok(())
    }
}
/// The collection of arguments to a given command or response.
///
/// AT supports multiple different formats, represented here by the different enum
/// branches.
#[derive(Debug, Clone, PartialEq)]
pub enum Arguments {
    /// A sequence of multiple arguments lists delimited by parentheses, like, `(1,2)(3,4)(a=1)`
    ParenthesisDelimitedArgumentLists(Vec<Vec<Argument>>),
    /// A single argument list delimited by commas, like, `1,2,a=3`
    ArgumentList(Vec<Argument>),
}

impl Arguments {
    fn write_comma_delimited_argument_list<W: io::Write>(
        &self,
        arguments: &[Argument],
        sink: &mut W,
    ) -> io::Result<()> {
        if !arguments.is_empty() {
            for argument in &arguments[0..arguments.len() - 1] {
                argument.write_to(sink)?;
                sink.write_all(b",")?;
            }
            arguments[arguments.len() - 1].write_to(sink)?;
        }

        Ok(())
    }

    fn write_paren_delimited_argument_lists<W: io::Write>(
        &self,
        argument_lists: &[Vec<Argument>],
        sink: &mut W,
    ) -> io::Result<()> {
        for arguments in argument_lists {
            sink.write_all(b"(")?;
            self.write_comma_delimited_argument_list(arguments, sink)?;
            sink.write_all(b")")?;
        }

        Ok(())
    }
}

impl WriteTo for Arguments {
    fn write_to<W: io::Write>(&self, sink: &mut W) -> io::Result<()> {
        match self {
            Arguments::ParenthesisDelimitedArgumentLists(argument_lists) => {
                self.write_paren_delimited_argument_lists(&argument_lists, sink)
            }
            Arguments::ArgumentList(argument_list) => {
                self.write_comma_delimited_argument_list(&argument_list, sink)
            }
        }
    }
}

/// An individual argument in a list.
#[derive(Debug, Clone, PartialEq)]
pub enum Argument {
    /// A primitive string or int.
    PrimitiveArgument(String),
    /// A key-value pair like `a=1`
    KeyValueArgument { key: String, value: String },
}

impl Argument {
    pub fn is_empty(&self) -> bool {
        match self {
            Argument::PrimitiveArgument(argument) => argument.is_empty(),
            Argument::KeyValueArgument { key, value } => key.is_empty() && value.is_empty(),
        }
    }
}

impl WriteTo for Argument {
    fn write_to<W: io::Write>(&self, sink: &mut W) -> io::Result<()> {
        match self {
            Argument::PrimitiveArgument(argument) => sink.write_all(argument.as_bytes())?,
            Argument::KeyValueArgument { key, value } => {
                sink.write_all(key.as_bytes())?;
                sink.write_all(b"=")?;
                sink.write_all(value.as_bytes())?;
            }
        }
        Ok(())
    }
}
