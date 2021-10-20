// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Context, Result};
use fuchsia_hash::Hash;
use fuchsia_pkg::{PackageManifest, PackagePath};
use fuchsia_url::pkg_url::PkgUrl;
use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;

/// A list of all packages included in an update, which can be written as a JSON
/// packages manifest.
/// TODO(fxbug.dev/76488): Share this enum with the rest of the SWD code.
///
/// ```json
/// {
///   "version": "1",
///   "content": [
///       "fuchsia-pkg://fuchsia.com/build-info/0?hash=30a83..",
///       "fuchsia-pkg://fuchsia.com/log_listener/0?hash=816c8..",
///     ...
///   ]
/// }
/// ```
#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
pub enum UpdatePackagesManifest {
    /// Version 1.
    #[serde(rename = "1")]
    V1(BTreeSet<PkgUrl>),
}

impl Default for UpdatePackagesManifest {
    fn default() -> Self {
        UpdatePackagesManifest::V1(BTreeSet::new())
    }
}

impl UpdatePackagesManifest {
    /// Add a package to be updated by its PackageManifest.
    pub fn add_by_manifest(&mut self, package: PackageManifest) -> Result<()> {
        let path = package.package_path();
        let meta_blob = package.into_blobs().into_iter().find(|blob| blob.path == "meta/");
        match meta_blob {
            Some(meta_blob) => self.add(path, meta_blob.merkle),
            _ => bail!(format!("Failed to find the meta far in package {}", path)),
        }
    }

    /// Add a package to be updated by its path and meta far merkle.
    pub fn add(&mut self, path: PackagePath, merkle: Hash) -> Result<()> {
        let path = format!("/{}", path.to_string());
        let url = PkgUrl::new_package("fuchsia.com".to_string(), path.clone(), Some(merkle))
            .context(format!("Failed to create package url for {}", path))?;
        self.add_by_url(url);
        Ok(())
    }

    /// Append all the |packages| to |self|.
    pub fn append(&mut self, packages: UpdatePackagesManifest) {
        match packages {
            UpdatePackagesManifest::V1(contents) => {
                for url in contents {
                    self.add_by_url(url);
                }
            }
        }
    }

    /// Add a new package by |url|.
    fn add_by_url(&mut self, url: PkgUrl) {
        match self {
            UpdatePackagesManifest::V1(contents) => contents.insert(url),
        };
    }
}

#[cfg(test)]
mod tests {
    use super::UpdatePackagesManifest;
    use serde_json::json;

    #[test]
    fn update_packages_manifest() {
        let mut manifest = UpdatePackagesManifest::default();
        manifest.add("one/0".parse().unwrap(), [0u8; 32].into()).unwrap();
        let out = serde_json::to_value(&manifest).unwrap();
        assert_eq!(
            out,
            json!({
                "version": "1",
                "content": [
                    "fuchsia-pkg://fuchsia.com/one/0?hash=0000000000000000000000000000000000000000000000000000000000000000"
                ],
            })
        );
    }
}
