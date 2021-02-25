// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module containing methods for generating code to "lower" from the high level, typed,
//! generated AT command and response types to the low level generic ASTs.

use {
    super::{
        common::{write_indent, TABSTOP},
        error::Result,
    },
    crate::definition::Definition,
    std::io,
};

/// Entry point to generate `lower` methods at a given indentation.
pub fn codegen<W: io::Write>(sink: &mut W, indent: u64, definitions: &[Definition]) -> Result {
    codegen_commands(sink, indent, &definitions)?;
    codegen_responses(sink, indent, &definitions)
}

fn codegen_commands<W: io::Write>(
    sink: &mut W,
    indent: u64,
    _definitions: &[Definition],
) -> Result {
    write_indented!(
        sink,
        indent,
        "pub fn lower_command(_highlevel: &highlevel::Command) -> lowlevel::Command {{\n"
    )?;
    write_indented!(sink, indent + TABSTOP, "unimplemented!()\n")?;
    write_indented!(sink, indent, "}}\n\n")?;

    Ok(())
}

fn codegen_responses<W: io::Write>(
    sink: &mut W,
    indent: u64,
    _definitions: &[Definition],
) -> Result {
    write_indented!(
        sink,
        indent,
        "pub fn lower_response(_highlevel: &highlevel::Response) -> lowlevel::Response {{\n"
    )?;
    write_indented!(sink, indent + TABSTOP, "unimplemented!()\n")?;
    write_indented!(sink, indent, "}}\n\n")?;

    Ok(())
}
