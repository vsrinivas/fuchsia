// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::generate_rust_test::CodeGenerator;

use anyhow::Result;
use std::io::prelude::*;

const TEMPLATE: &'static str = include_str!("templates/template_BUILD");

pub struct BuildGenerator {
    pub test_program_name: String,
    pub component_name: String,
}

impl CodeGenerator for BuildGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        // TODO(yuanzhi) We should also figure out how to auto-generate COMPONENT_FIDL_BUILD_TARGET
        let content = TEMPLATE
            .replace("BINARY_NAME", &self.test_program_name)
            .replace("COMPONENT_NAME", &self.component_name);

        writer.write_all(&content.as_bytes())?;
        Ok(())
    }
}
