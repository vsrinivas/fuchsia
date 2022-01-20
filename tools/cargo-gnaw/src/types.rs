// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::gn::add_version_suffix,
    anyhow::{anyhow, Error},
    cargo_metadata::Package,
    std::convert::TryFrom,
};

pub type Feature = String;
pub type Platform = String;

#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub enum GnRustType {
    ProcMacro,
    Library,
    Rlib,
    Staticlib,
    Dylib,
    Cdylib,
    Binary,
    Example,
    Test,
    Bench,
    BuildScript,
}

impl<'a> TryFrom<&'a str> for GnRustType {
    type Error = Error;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        match value {
            "bin" => Ok(GnRustType::Binary),
            "lib" => Ok(GnRustType::Library),
            "rlib" => Ok(GnRustType::Rlib),
            "staticlib" => Ok(GnRustType::Staticlib),
            "dylib" => Ok(GnRustType::Dylib),
            "cdylib" => Ok(GnRustType::Cdylib),
            "proc-macro" => Ok(GnRustType::ProcMacro),
            "test" => Ok(GnRustType::Test),
            "example" => Ok(GnRustType::Example),
            "bench" => Ok(GnRustType::Bench),
            "custom-build" => Ok(GnRustType::BuildScript),
            value => Err(anyhow!("unknown crate type: {}", value)),
        }
    }
}

pub trait GnData {
    fn gn_name(&self) -> String;
    fn is_proc_macro(&self) -> bool;
}

impl GnData for Package {
    fn gn_name(&self) -> String {
        add_version_suffix(&self.name, &self.version)
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
