// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains the entry point for all code generation.  It creates some
//! modules and imports and calls out to the other codegen modules to generate types
//! and "raise" and "lower" methods.

use {
    super::lower as codegen_lower,
    super::raise as codegen_raise,
    super::types as codegen_types,
    super::{
        common::{write_indent, write_newline, TABSTOP},
        error::Result,
    },
    crate::definition::Definition,
    std::io,
};

static PRELUDE: &str = r"// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is generated code.  Please do not edit it; instead edit the AT
// command definitions used to generate it.";

pub fn codegen<W: io::Write>(sink: &mut W, definitions: &[Definition]) -> Result {
    let indent = 0;

    sink.write_all(PRELUDE.as_bytes())?;
    write_newline(sink)?;
    write_newline(sink)?;

    write_indented!(sink, indent, "pub mod types {{\n")?;
    write_indented!(sink, indent + TABSTOP, "use num_derive::FromPrimitive;\n\n")?;

    codegen_types::codegen(sink, indent + TABSTOP, definitions)?;

    write_indented!(sink, indent, "}}\n\n")?;
    write_indented!(sink, indent, "pub mod translate {{\n")?;
    write_indented!(sink, indent + TABSTOP, "use crate::lowlevel;\n")?;
    write_indented!(sink, indent + TABSTOP, "use crate::generated::types as highlevel;\n")?;
    write_indented!(sink, indent + TABSTOP, "use crate::serde::DeserializeError;\n")?;
    write_indented!(sink, indent + TABSTOP, "use crate::translate_util::*;\n")?;
    write_indented!(sink, indent + TABSTOP, "use num_traits::FromPrimitive;\n\n")?;

    codegen_raise::codegen(sink, indent + TABSTOP, definitions)?;
    codegen_lower::codegen(sink, indent + TABSTOP, definitions)?;

    write_indented!(sink, indent, "}}\n")?;

    Ok(())
}
