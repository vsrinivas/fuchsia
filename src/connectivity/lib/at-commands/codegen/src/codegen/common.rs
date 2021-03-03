// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Code common to all code generation modules.

#![macro_use]

use {super::error::Result, std::io};

pub mod type_names {
    pub static HIGHLEVEL_COMMAND_TYPE: &str = "Command";
    pub static HIGHLEVEL_SUCCESS_TYPE: &str = "Success";

    pub static LOWLEVEL_COMMAND_TYPE: &str = "Command";
    pub static LOWLEVEL_EXECUTE_COMMAND_VARIANT: &str = "Execute";
    pub static LOWLEVEL_READ_COMMAND_VARIANT: &str = "Read";
    pub static LOWLEVEL_TEST_COMMAND_VARIANT: &str = "Test";

    pub static LOWLEVEL_RESPONSE_TYPE: &str = "Response";
    pub static LOWLEVEL_RESPONSE_VARIANT: &str = "Success";
}

/// The amount to increase indentation when generating a new block.
pub static TABSTOP: u64 = 4;

/// Convert a string to all lowercase except for an initial capital
pub fn to_initial_capital(str: &str) -> String {
    let mut chars = str.chars();
    let head = chars.next();
    if let Some(head) = head {
        let tail = chars.as_str();
        format!("{}{}", head.to_uppercase(), tail.to_lowercase())
    } else {
        String::from("")
    }
}

/// Utility function for writing a newline.
pub fn write_newline<W: io::Write>(sink: &mut W) -> Result {
    sink.write_all("\n".as_bytes())?;

    Ok(())
}

/// Utility function for writing an indentation of a certain width.
pub fn write_indent<W: io::Write>(sink: &mut W, indent: u64) -> Result {
    let bytes = vec![b' '; indent as usize];
    sink.write_all(&bytes)?;

    Ok(())
}

/// Macro to write a formatted string at a certain indentation level as bytes.
macro_rules! write_indented {
    ($sink: expr, $indent: expr, $($format_args: tt)*) => {
        {
            write_indent($sink, $indent)?;
            let formatted_string = format!($($format_args)*);
            ($sink).write_all(formatted_string.as_bytes())
        }
    }
}

/// Macro to write a formatted string as bytes.
macro_rules! write_no_indent {
    ($sink: expr, $($format_args: tt)*) => {
        {
          let formatted_string = format!($($format_args)*);
          ($sink).write_all(formatted_string.as_bytes())
        }
    }
}

/// Write an item definiton block with an initial type and name.
pub fn codegen_block<W: io::Write, I: FnOnce(&mut W, u64) -> Result>(
    sink: &mut W,
    indent: u64,
    item: Option<&str>,
    name: &str,
    interior: I,
    trailing_char: Option<&str>,
) -> Result {
    write_indent(sink, indent)?;
    item.map(|item| {
        sink.write_all(item.as_bytes())?;
        sink.write_all(" ".as_bytes())
    })
    .unwrap_or(Ok(()))?;
    sink.write_all(name.as_bytes())?;
    sink.write_all(" {".as_bytes())?;
    write_newline(sink)?;

    interior(sink, indent + 4)?;

    write_indented!(sink, indent, "}}")?;
    trailing_char
        .map(|trailing_char| sink.write_all(trailing_char.as_bytes()))
        .unwrap_or(Ok(()))?;
    write_newline(sink)
}
