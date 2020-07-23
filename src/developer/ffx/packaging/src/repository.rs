// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use ffx_config::constants::PACKAGE_REPO;
use ffx_config::get;
use std::{fs, path};

#[derive(Clone)]
pub struct BlobsDir {
    dir: path::PathBuf,
}

pub struct Repository {
    blobs: BlobsDir,
    // This will be used eventually to store published packages
    #[allow(dead_code)]
    packages_dir: path::PathBuf,
}

impl BlobsDir {
    fn new(dir: path::PathBuf) -> BlobsDir {
        fs::create_dir_all(&dir).unwrap();
        BlobsDir { dir }
    }

    pub fn add_blob<F>(&self, mut blob: F) -> Result<fuchsia_merkle::Hash, Error>
    where
        F: std::io::Read + std::io::Seek,
    {
        let hash = fuchsia_merkle::MerkleTree::from_reader(&mut blob)?.root();
        blob.seek(std::io::SeekFrom::Start(0))?;
        let blob_path = self.dir.join(hash.to_string());
        if !blob_path.exists() {
            let mut copy = fs::File::create(blob_path)?;
            std::io::copy(&mut blob, &mut copy)?;
        }
        Ok(hash)
    }
}

impl Repository {
    pub fn new(dir: path::PathBuf, blobs: path::PathBuf) -> Repository {
        let packages_dir = dir.join("packages");
        fs::create_dir_all(&packages_dir).unwrap();
        Repository { blobs: BlobsDir::new(blobs), packages_dir }
    }

    pub async fn default_repo() -> Repository {
        let repo_dir: path::PathBuf = get!(str, PACKAGE_REPO, "").await.into();
        Repository::new(repo_dir.clone(), repo_dir.join("blobs"))
    }

    pub fn blobs(&self) -> &BlobsDir {
        &self.blobs
    }
}

#[cfg(test)]
mod test {
    #[cfg(target_os = "linux")]
    use {crate::repository::Repository, anyhow::Error, std::path};

    #[fuchsia_async::run_singlethreaded(test)]
    #[cfg(target_os = "linux")]
    async fn test_package_repo_config() -> Result<(), Error> {
        let default_repo_dir = Repository::default_repo().await.packages_dir;
        let expected_repo_dir =
            path::PathBuf::from(std::env::var("HOME")?).join(".local/share/ffx/packages");
        assert_eq!(default_repo_dir, expected_repo_dir);
        Ok(())
    }
}
