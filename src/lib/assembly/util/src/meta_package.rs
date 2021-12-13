// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::path_to_string::PathToStringExt;

use anyhow::Result;
use fuchsia_pkg::MetaPackage;
use std::convert::TryInto as _;
use std::fs::{create_dir_all, File};
use std::path::Path;

/// Construct a meta/package file in `gendir`.
pub fn create_meta_package_file(
    gendir: impl AsRef<Path>,
    name: impl Into<String>,
    variant: impl Into<String>,
) -> Result<String> {
    let package_name = name.into();
    let meta_package_path = gendir.as_ref().join(&package_name).join("meta/package");
    if let Some(parent_dir) = meta_package_path.parent() {
        create_dir_all(parent_dir)?;
    }

    let file = File::create(&meta_package_path)?;
    let meta_package =
        MetaPackage::from_name_and_variant(package_name.try_into()?, variant.into().try_into()?);
    meta_package.serialize(file)?;
    meta_package_path.path_to_string()
}

#[cfg(test)]
mod tests {
    use crate::path_to_string::PathToStringExt;

    use super::create_meta_package_file;
    use tempfile::TempDir;

    #[test]
    fn test_create_meta_package_file() {
        let gen_dir = TempDir::new().unwrap();
        let name = "system_image";
        let meta_package_path = gen_dir.as_ref().join(&name).join("meta/package");
        let created_path = create_meta_package_file(gen_dir, name, "0").unwrap();
        assert_eq!(created_path, meta_package_path.path_to_string().unwrap());
    }
}
