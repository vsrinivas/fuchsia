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
        common::{write_indent, write_newline},
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

pub fn codegen<W: io::Write>(
    type_sink: &mut W,
    translate_sink: &mut W,
    definitions: &[Definition],
) -> Result {
    let indent = 0;

    // Write type definitions.
    type_sink.write_all(PRELUDE.as_bytes())?;
    write_newline(type_sink)?;
    write_newline(type_sink)?;

    write_indented!(type_sink, indent, "use num_derive::FromPrimitive;\n\n")?;

    codegen_types::codegen(type_sink, indent, definitions)?;

    // Write generated translation code.
    translate_sink.write_all(PRELUDE.as_bytes())?;
    write_newline(translate_sink)?;
    write_newline(translate_sink)?;

    write_indented!(translate_sink, indent, "use crate::lowlevel;\n")?;
    write_indented!(translate_sink, indent, "use crate::highlevel;\n")?;
    write_indented!(translate_sink, indent, "use crate::serde::DeserializeErrorCause;\n")?;
    write_indented!(translate_sink, indent, "use crate::translate_util::*;\n")?;
    write_indented!(translate_sink, indent, "use num_traits::FromPrimitive;\n\n")?;

    codegen_raise::codegen(translate_sink, indent, definitions)?;
    codegen_lower::codegen(translate_sink, indent, definitions)?;

    Ok(())
}
