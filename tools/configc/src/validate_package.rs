// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use std::path::PathBuf;

/// validate that any components in the package which declare structured config are able to have
/// their configuration resolved from their package
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "validate-package")]
pub struct ValidatePackage {
    /// path to the package manifest to validate
    #[argh(positional)]
    package: PathBuf,

    /// path to the stamp file to write when done validating
    #[argh(option)]
    stamp: PathBuf,
}

impl ValidatePackage {
    pub fn validate(self) -> Result<()> {
        if let Err(e) = assembly_validate_product::validate_package(&self.package) {
            anyhow::bail!("Failed to validate {}:{}", self.package.display(), e);
        }
        std::fs::write(&self.stamp, &[])
            .with_context(|| format!("writing stamp file to {}", self.stamp.display()))?;
        Ok(())
    }
}
