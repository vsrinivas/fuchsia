// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::test_code::CodeGenerator;
use anyhow::Result;
use chrono::Datelike;
use std::io::prelude::*;

const RUST_TEMPLATE: &'static str = include_str!("templates/template_rust_manifest");
const CPP_TEMPLATE: &'static str = include_str!("templates/template_cpp_manifest");

pub struct RustManifestGenerator {
    pub test_program_name: String,
}

impl CodeGenerator for RustManifestGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        let current_date = chrono::Utc::now();
        let year = current_date.year();
        let content = RUST_TEMPLATE
            .replace("BINARY_NAME", &self.test_program_name)
            .replace("CURRENT_YEAR", &year.to_string());

        writer.write_all(&content.as_bytes())?;
        Ok(())
    }
}

pub struct CppManifestGenerator {
    pub test_program_name: String,
}

impl CodeGenerator for CppManifestGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        let current_date = chrono::Utc::now();
        let year = current_date.year();
        let content = CPP_TEMPLATE
            .replace("BINARY_NAME", &self.test_program_name)
            .replace("CURRENT_YEAR", &year.to_string());

        writer.write_all(&content.as_bytes())?;
        Ok(())
    }
}
