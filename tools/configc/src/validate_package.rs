// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use camino::Utf8PathBuf;
use fuchsia_pkg::PackageManifest;

/// validate that any components in the package which declare structured config are able to have
/// their configuration resolved from their package
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "validate-package")]
pub struct ValidatePackage {
    /// path to the package manifest to validate
    #[argh(positional)]
    package: Utf8PathBuf,

    /// path to the stamp file to write when done validating
    #[argh(option)]
    stamp: Utf8PathBuf,
}

impl ValidatePackage {
    pub fn validate(self) -> Result<()> {
        let package = PackageManifest::try_load_from(&self.package)
            .with_context(|| format!("reading {}", self.package))?;
        if let Err(e) = assembly_validate_product::validate_package(&package) {
            anyhow::bail!("Failed to validate package `{}`:{}", package.name(), e);
        }
        std::fs::write(&self.stamp, &[])
            .with_context(|| format!("writing stamp file to {}", self.stamp))?;
        Ok(())
    }
}
