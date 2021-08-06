// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::generate_rust_test::CodeGenerator;
use anyhow::Result;
use std::io::prelude::*;

const TEMPLATE: &'static str = include_str!("templates/template_manifest");

pub struct ManifestGenerator {
    pub test_program_name: String,
}

impl CodeGenerator for ManifestGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        let content = TEMPLATE.replace("BINARY_NAME", &self.test_program_name);
        writer.write_all(&content.as_bytes())?;
        Ok(())
    }
}
