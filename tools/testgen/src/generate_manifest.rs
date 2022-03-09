// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::test_code::{copyright, CodeGenerator};
use anyhow::Result;
use std::io::prelude::*;

const RUST_TEMPLATE: &'static str = include_str!("templates/template_rust_manifest");
const CPP_TEMPLATE: &'static str = include_str!("templates/template_cpp_manifest");

pub struct RustManifestGenerator {
    pub test_program_name: String,
    pub copyright: bool,
}

impl CodeGenerator for RustManifestGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        let mut content = "".to_string();
        if self.copyright {
            content.push_str(&copyright("//"))
        }
        content.push_str(&RUST_TEMPLATE.replace("BINARY_NAME", &self.test_program_name));
        writer.write_all(&content.as_bytes())?;
        Ok(())
    }
}

pub struct CppManifestGenerator {
    pub test_program_name: String,
    pub copyright: bool,
}

impl CodeGenerator for CppManifestGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        let mut content = "".to_string();
        if self.copyright {
            content.push_str(&copyright("//"))
        }
        content.push_str(&CPP_TEMPLATE.replace("BINARY_NAME", &self.test_program_name));
        writer.write_all(&content.as_bytes())?;
        Ok(())
    }
}
