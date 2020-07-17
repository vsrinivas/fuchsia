// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use lazy_static::lazy_static;
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

lazy_static! {
    pub static ref DATA_DIR: path::PathBuf = home::home_dir().unwrap().join(".local/share/ffx");
    pub static ref DEFAULT_REPO: Repository = Repository::new(DATA_DIR.clone());
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
    pub fn new(dir: path::PathBuf) -> Repository {
        Self::new_with_blobs(dir, DATA_DIR.join("blobs"))
    }
    pub fn new_with_blobs(dir: path::PathBuf, blobs: path::PathBuf) -> Repository {
        let packages_dir = dir.join("packages");
        fs::create_dir_all(&packages_dir).unwrap();
        Repository { blobs: BlobsDir::new(blobs), packages_dir }
    }

    pub fn blobs(&self) -> &BlobsDir {
        &self.blobs
    }
}
