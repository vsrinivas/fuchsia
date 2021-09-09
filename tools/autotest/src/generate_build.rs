// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::test_code::CodeGenerator;
use anyhow::Result;
use chrono::Datelike;
use std::io::prelude::*;

const RUST_TEMPLATE: &'static str = include_str!("templates/template_rust_BUILD");
const CPP_TEMPLATE: &'static str = include_str!("templates/template_cpp_BUILD");

pub struct RustBuildGenerator {
    pub test_program_name: String,
    pub component_name: String,
}

impl CodeGenerator for RustBuildGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        let current_date = chrono::Utc::now();
        let year = current_date.year();
        // TODO(yuanzhi) We should also figure out how to auto-generate COMPONENT_FIDL_BUILD_TARGET
        let content = RUST_TEMPLATE
            .replace("BINARY_NAME", &self.test_program_name)
            .replace("COMPONENT_NAME", &self.component_name)
            .replace("CURRENT_YEAR", &year.to_string());

        writer.write_all(&content.as_bytes())?;
        Ok(())
    }
}

pub struct CppBuildGenerator {
    pub test_program_name: String,
    pub component_name: String,
}

impl CodeGenerator for CppBuildGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        let current_date = chrono::Utc::now();
        let year = current_date.year();
        // TODO(yuanzhi) We should also figure out how to auto-generate COMPONENT_FIDL_BUILD_TARGET
        let content = CPP_TEMPLATE
            .replace("BINARY_NAME", &self.test_program_name)
            .replace("COMPONENT_NAME", &self.component_name)
            .replace("CURRENT_YEAR", &year.to_string());

        writer.write_all(&content.as_bytes())?;
        Ok(())
    }
}
