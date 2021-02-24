// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains code to generate types representing AT commands and responses.

use {
    super::{common::write_indent, error::Result},
    crate::definition::Definition,
    std::io,
};

pub fn codegen<W: io::Write>(sink: &mut W, indent: i64, _definitions: &[Definition]) -> Result {
    write_indented!(sink, indent, "pub enum Command {{ }}\n\n")?;
    write_indented!(sink, indent, "pub enum Response {{ }}\n\n")?;

    Ok(())
}
