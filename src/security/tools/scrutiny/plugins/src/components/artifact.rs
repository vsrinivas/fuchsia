// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::components::package_getter::PackageGetter,
    std::fs,
    std::io::{Error, ErrorKind, Result},
    std::path::{Path, PathBuf},
};

pub struct ArtifactGetter {
    artifact_path: PathBuf,
}

/// The ArtifactGetter retrieves package data and blobs directly from the
/// build artifacts on disk.
impl ArtifactGetter {
    pub fn new(artifact_path: &Path) -> Self {
        Self { artifact_path: artifact_path.to_path_buf() }
    }
}

impl PackageGetter for ArtifactGetter {
    fn read_raw(&self, path: &str) -> Result<Vec<u8>> {
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

        let data = fs::read(self.artifact_path.join(path))?;
        Ok(data)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::fs::File, std::io::prelude::*, tempfile::tempdir};

    #[test]
    fn test_basic() {
        let dir = tempdir().unwrap().into_path();
        let getter = ArtifactGetter::new(&dir);
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
        let getter = ArtifactGetter::new(&dir.into_path());
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
}
