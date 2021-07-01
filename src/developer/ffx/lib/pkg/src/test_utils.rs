// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::repository::{FileSystemRepository, Repository, RepositoryKeyConfig},
    anyhow::{anyhow, Context, Result},
    std::{
        fs::{copy, create_dir},
        path::PathBuf,
    },
    walkdir::WalkDir,
};

const EMPTY_REPO_PATH: &str = "host_x64/test_data/ffx_lib_pkg/empty-repo";

pub fn repo_key() -> RepositoryKeyConfig {
    RepositoryKeyConfig::Ed25519Key(
        [
            29, 76, 86, 76, 184, 70, 108, 73, 249, 127, 4, 47, 95, 63, 36, 35, 101, 255, 212, 33,
            10, 154, 26, 130, 117, 157, 125, 88, 175, 214, 109, 113,
        ]
        .to_vec(),
    )
}

fn copy_dir(from: PathBuf, to: PathBuf) -> Result<()> {
    let walker = WalkDir::new(from.clone());
    for entry in walker.into_iter() {
        let entry = entry?;
        let to_path = to.join(entry.path().strip_prefix(from.clone())?);
        if entry.metadata()?.is_dir() {
            create_dir(to_path.clone()).context(format!("creating {:?}", to_path))?;
        } else {
            copy(entry.path(), to_path.clone()).context(format!(
                "copying {:?} to {:?}",
                entry.path(),
                to_path
            ))?;
        }
    }

    Ok(())
}

pub async fn make_readonly_empty_repository(name: &str) -> Result<Repository> {
    let backend = FileSystemRepository::new(PathBuf::from(EMPTY_REPO_PATH));
    Repository::new(name, Box::new(backend)).await.map_err(|e| anyhow!(e))
}

pub async fn make_writable_empty_repository(name: &str, root: PathBuf) -> Result<Repository> {
    copy_dir(PathBuf::from(EMPTY_REPO_PATH).canonicalize()?, root.clone())?;

    let backend = FileSystemRepository::new(root);
    Ok(Repository::new(name, Box::new(backend)).await?)
}
