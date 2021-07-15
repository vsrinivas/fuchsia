// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants::PACKAGE_REPO;
use crate::tuf_repo::TufRepo;
use anyhow::Result;
use std::fs::{create_dir_all, File};
use std::io;
use std::io::{Read, Seek, SeekFrom};
use std::path::PathBuf;

#[derive(Clone)]
pub struct BlobsDir {
    dir: PathBuf,
}

pub struct Repository {
    blobs: BlobsDir,
    _tuf_repo: TufRepo,
}

impl BlobsDir {
    fn new(dir: PathBuf) -> BlobsDir {
        create_dir_all(&dir).unwrap();
        BlobsDir { dir }
    }

    pub fn add_blob(&self, mut blob: impl Read + Seek) -> Result<fuchsia_merkle::Hash> {
        let hash = fuchsia_merkle::MerkleTree::from_reader(&mut blob)?.root();
        blob.seek(SeekFrom::Start(0))?;
        let blob_path = self.dir.join(hash.to_string());
        if !blob_path.exists() {
            let mut copy = File::create(blob_path)?;
            io::copy(&mut blob, &mut copy)?;
        }
        Ok(hash)
    }

    pub fn open_blob(&self, hash: &fuchsia_merkle::Hash) -> Result<File> {
        Ok(File::open(self.dir.join(hash.to_string()))?)
    }
}

impl Repository {
    pub fn new(dir: PathBuf, blobs: PathBuf) -> Result<Repository> {
        Ok(Repository {
            blobs: BlobsDir::new(blobs),
            _tuf_repo: TufRepo::new(dir.join("tuf"), dir.join("keys"))?,
        })
    }

    pub async fn default_repo() -> Result<Repository> {
        let repo_dir = PathBuf::from(PACKAGE_REPO);
        Repository::new(repo_dir.clone(), repo_dir.join("blobs"))
    }

    pub fn blobs(&self) -> &BlobsDir {
        &self.blobs
    }
}

#[cfg(test)]
mod test {
    #[cfg(target_os = "linux")]
    use {crate::repository::Repository, anyhow::Result, std::path};

    #[fuchsia_async::run_singlethreaded(test)]
    #[cfg(target_os = "linux")]
    async fn test_package_repo_config() -> Result<()> {
        let repo = Repository::default_repo().await?;
        let default_blobs_dir = &repo.blobs().dir;
        let expected_blobs_dir =
            PathBuf::from(std::env::var("HOME")?).join(".local/share/ffx/blobs");
        assert_eq!(default_blobs_dir, &expected_blobs_dir);
        Ok(())
    }
}
