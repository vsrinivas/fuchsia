// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants::PACKAGE_REPO;
use crate::tuf_repo::TufRepo;
use anyhow::Result;
use ffx_config::get;
use std::io::{Read, Seek};
use std::{fs, io, path};

#[derive(Clone)]
pub struct BlobsDir {
    dir: path::PathBuf,
}

pub struct Repository {
    blobs: BlobsDir,
    _tuf_repo: TufRepo,
}

impl BlobsDir {
    fn new(dir: path::PathBuf) -> BlobsDir {
        fs::create_dir_all(&dir).unwrap();
        BlobsDir { dir }
    }

    pub fn add_blob(&self, mut blob: impl Read + Seek) -> Result<fuchsia_merkle::Hash> {
        let hash = fuchsia_merkle::MerkleTree::from_reader(&mut blob)?.root();
        blob.seek(std::io::SeekFrom::Start(0))?;
        let blob_path = self.dir.join(hash.to_string());
        if !blob_path.exists() {
            let mut copy = fs::File::create(blob_path)?;
            io::copy(&mut blob, &mut copy)?;
        }
        Ok(hash)
    }

    pub fn open_blob(&self, hash: &fuchsia_merkle::Hash) -> Result<fs::File> {
        Ok(fs::File::open(self.dir.join(hash.to_string()))?)
    }
}

impl Repository {
    pub fn new(dir: path::PathBuf, blobs: path::PathBuf) -> Result<Repository> {
        Ok(Repository {
            blobs: BlobsDir::new(blobs),
            _tuf_repo: TufRepo::new(dir.join("tuf"), dir.join("keys"))?,
        })
    }

    pub async fn default_repo() -> Result<Repository> {
        let default_dir: path::PathBuf = path::PathBuf::from("");
        let repo_dir: path::PathBuf = get(PACKAGE_REPO).await.unwrap_or(default_dir);
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
            path::PathBuf::from(std::env::var("HOME")?).join(".local/share/ffx/blobs");
        assert_eq!(default_blobs_dir, &expected_blobs_dir);
        Ok(())
    }
}
