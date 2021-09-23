// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::package::getter::PackageGetter,
    std::fs,
    std::io::{Error, ErrorKind, Result},
    std::{
        collections::HashSet,
        path::{Path, PathBuf},
    },
};

pub struct ArtifactGetter {
    artifact_path: PathBuf,
    deps: HashSet<String>,
}

/// The ArtifactGetter retrieves package data and blobs directly from the
/// build artifacts on disk.
impl ArtifactGetter {
    pub fn new(artifact_path: &Path) -> Self {
        Self { artifact_path: artifact_path.to_path_buf(), deps: HashSet::new() }
    }
}

impl PackageGetter for ArtifactGetter {
    fn read_raw(&mut self, path: &str) -> Result<Vec<u8>> {
        let path = self.artifact_path.join(path);
        if path.is_relative() {
            return Err(Error::new(ErrorKind::PermissionDenied, "Relative paths disabled"));
        }
        if path.is_dir() {
            return Err(Error::new(ErrorKind::PermissionDenied, "Directories disabled"));
        }
        if !path.starts_with(&self.artifact_path) {
            return Err(Error::new(
                ErrorKind::PermissionDenied,
                "Path outside of repository directory",
            ));
        }

        let path = self.artifact_path.join(path);
        let path_str = path.to_str();
        if path_str.is_none() {
            return Err(Error::new(
                ErrorKind::InvalidInput,
                "Path could not be converted to string",
            ));
        };
        let path_str = path_str.unwrap();

        let data = fs::read(path_str)?;
        self.deps.insert(path_str.to_string());

        Ok(data)
    }

    fn get_deps(&self) -> HashSet<String> {
        self.deps.clone()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, maplit::hashset, std::fs::File, std::io::prelude::*, tempfile::tempdir};

    #[test]
    fn test_basic() {
        let dir = tempdir().unwrap().into_path();
        let mut getter = ArtifactGetter::new(&dir);
        let mut file = File::create(dir.join("foo")).unwrap();
        file.write_all(b"test_data").unwrap();
        file.sync_all().unwrap();
        let result = getter.read_raw("foo");
        assert_eq!(result.is_ok(), true);
        let data = result.unwrap();
        assert_eq!(data, b"test_data");
    }

    #[test]
    fn test_relative_path_denied() {
        let dir = tempdir().unwrap();
        let mut getter = ArtifactGetter::new(&dir.into_path());
        let result = getter.read_raw("..");
        match result {
            Ok(_) => {
                // This path should never be taken with a relative path.
                assert_eq!(true, false);
            }
            Err(error) => {
                assert_eq!(error.kind(), ErrorKind::PermissionDenied);
            }
        };
    }

    #[test]
    fn test_deps() {
        let dir = tempdir().unwrap().into_path();
        let mut getter = ArtifactGetter::new(&dir);

        let foo_path = dir.join("foo");
        let mut file = File::create(&foo_path).unwrap();
        file.write_all(b"test_data").unwrap();
        file.sync_all().unwrap();
        let foo_path_str = foo_path.to_str().unwrap();

        let bar_path = dir.join("bar");
        let mut file = File::create(&bar_path).unwrap();
        file.write_all(b"test_data").unwrap();
        file.sync_all().unwrap();
        let bar_path_str = bar_path.to_str().unwrap();

        assert_eq!(getter.read_raw("foo").is_ok(), true);
        assert_eq!(getter.read_raw("bar").is_ok(), true);
        assert_eq!(getter.read_raw("foo").is_ok(), true);
        let deps = getter.get_deps();
        assert_eq!(deps, hashset! {foo_path_str.to_string(), bar_path_str.to_string()});
    }
}
