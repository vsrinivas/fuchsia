// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use fuchsia_pkg::MetaPackage;
use std::convert::TryInto as _;
use std::fs::{create_dir_all, File};
use std::path::Path;

/// Extension trait for getting a String from a Path.
pub trait PathToStringExt {
    /// Convert the path to a string if it is valid UTF-8, and return an error otherwise.
    fn path_to_string(&self) -> Result<String>;
}

impl PathToStringExt for Path {
    fn path_to_string(&self) -> Result<String> {
        self.to_str()
            .context(format!("Path is not valid UTF-8: {}", self.display()))
            .map(str::to_string)
    }
}

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
    use super::*;
    use std::ffi::OsString;
    use std::os::unix::ffi::OsStringExt;
    use std::path::PathBuf;
    use tempfile::TempDir;

    #[test]
    fn path_to_string() {
        let path = PathBuf::from("/some/path/to/file.txt");
        let string = path.path_to_string().unwrap();
        assert_eq!(string, "/some/path/to/file.txt".to_string());
    }

    #[test]
    fn invalid_path_to_string() {
        let invalid_path = PathBuf::from(OsString::from_vec(b"invalid\xe7".to_vec()));
        assert!(invalid_path.path_to_string().is_err());
    }

    #[test]
    fn test_create_meta_package_file() {
        let gen_dir = TempDir::new().unwrap();
        let name = "system_image";
        let meta_package_path = gen_dir.as_ref().join(&name).join("meta/package");
        let created_path = create_meta_package_file(gen_dir, name, "0").unwrap();
        assert_eq!(created_path, meta_package_path.path_to_string().unwrap());
    }
}
