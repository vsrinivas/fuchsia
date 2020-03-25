// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    cargo_metadata::Package,
    std::convert::TryFrom,
};

pub type Feature = String;
pub type Platform = String;

#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
pub enum GnRustType {
    ProcMacro,
    Library,
    StaticLibrary,
    Binary,
    Example,
    Test,
    Bench,
    BuildScript,
}

impl TryFrom<&Vec<String>> for GnRustType {
    type Error = Error;

    fn try_from(value: &Vec<String>) -> Result<Self, Self::Error> {
        if value.len() != 1 {
            return Err(anyhow!("malformed crate type description. Expects vector of length 1"));
        }
        let internal = &value[0];
        match internal.as_str() {
            "bin" => Ok(GnRustType::Binary),
            "lib" => Ok(GnRustType::Library),
            "rlib" => Ok(GnRustType::StaticLibrary),
            "proc-macro" => Ok(GnRustType::ProcMacro),
            "test" => Ok(GnRustType::Test),
            "example" => Ok(GnRustType::Example),
            "bench" => Ok(GnRustType::Bench),
            "custom-build" => Ok(GnRustType::BuildScript),
            err => Err(anyhow!("unknown crate type: {}", err)),
        }
    }
}

pub trait GnData {
    fn gn_name(&self) -> String;
    fn is_proc_macro(&self) -> bool;
}

impl GnData for Package {
    fn gn_name(&self) -> String {
        let mut accum = String::new();
        accum.push_str(&self.name);
        accum.push_str("-");
        accum.push_str(self.version.to_string().replace(".", "-").as_str());
        accum
    }

    fn is_proc_macro(&self) -> bool {
        for target in &self.targets {
            for kind in &target.kind {
                if kind == "proc-macro" {
                    return true;
                }
            }
        }
        false
    }
}
