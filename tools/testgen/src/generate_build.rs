// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::test_code::{copyright, CodeGenerator};
use anyhow::Result;
use std::io::prelude::*;

const RUST_TEMPLATE: &'static str = include_str!("templates/template_rust_BUILD");
const CPP_TEMPLATE: &'static str = include_str!("templates/template_cpp_BUILD");

pub struct RustBuildGenerator {
    pub test_program_name: String,
    pub component_name: String,
    pub mock: bool,
    pub copyright: bool,
}

impl CodeGenerator for RustBuildGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        // TODO(yuanzhi) We should also figure out how to auto-generate COMPONENT_FIDL_BUILD_TARGET
        let mut content = "".to_string();
        if self.copyright {
            content.push_str(&copyright("#"))
        }
        content.push_str(
            &RUST_TEMPLATE
                .replace("BINARY_NAME", &self.test_program_name)
                .replace("TEST_PACKAGE_NAME", &self.test_program_name),
        );
        if self.mock {
            content = content
                .replace("EXTRA_MOCK_DEPS", "\n    \"//third_party/rust_crates:async-trait\",");
        } else {
            content = content.replace("EXTRA_MOCK_DEPS", "");
        }
        writer.write_all(&content.as_bytes())?;
        Ok(())
    }
}

pub struct CppBuildGenerator {
    pub test_program_name: String,
    pub component_name: String,
    pub copyright: bool,
}

impl CodeGenerator for CppBuildGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        let mut content = "".to_string();
        if self.copyright {
            content.push_str(&copyright("#"))
        }
        // TODO(yuanzhi) We should also figure out how to auto-generate COMPONENT_FIDL_BUILD_TARGET
        content.push_str(
            &CPP_TEMPLATE
                .replace("BINARY_NAME", &self.test_program_name)
                .replace("TEST_PACKAGE_NAME", &self.test_program_name),
        );
        writer.write_all(&content.as_bytes())?;
        Ok(())
    }
}
