// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Code common to all code generation modules.

#![macro_use]

use {super::error::Result, std::io};

/// The amount to increase indentation when generating a new block.
pub static TABSTOP: i64 = 4;

/// Utility method for writing a newline.
pub fn write_newline<W: io::Write>(sink: &mut W) -> Result {
    sink.write_all("\n".as_bytes())?;

    Ok(())
}

/// Utility method for writing an indentation of a certain width.
pub fn write_indent<W: io::Write>(sink: &mut W, indent: i64) -> Result {
    let bytes = vec![b' '; indent as usize];
    sink.write_all(&bytes)?;

    Ok(())
}

/// Macro to write a formatted string at a certain indentation level.
macro_rules! write_indented {
    ($sink: expr, $indent: expr, $($format_args: tt)*) => {
        {
            write_indent($sink, $indent)?;
            let formatted_string = format!($($format_args)*);
            ($sink).write_all(formatted_string.as_bytes())
        }
    }
}
