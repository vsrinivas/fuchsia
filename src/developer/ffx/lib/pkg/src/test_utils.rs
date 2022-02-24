// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::repository::{PmRepository, Repository, RepositoryKeyConfig},
    anyhow::{anyhow, Context, Result},
    camino::Utf8PathBuf,
    std::{
        fs::{copy, create_dir},
        path::{Path, PathBuf},
    },
    tuf::crypto::Ed25519PrivateKey,
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

pub fn repo_private_key() -> Ed25519PrivateKey {
    Ed25519PrivateKey::from_ed25519(&[
        80, 121, 161, 145, 5, 165, 178, 98, 248, 146, 132, 195, 60, 32, 72, 122, 150, 223, 124,
        216, 217, 43, 74, 9, 221, 38, 156, 113, 181, 63, 234, 98, 190, 11, 152, 63, 115, 150, 218,
        103, 92, 64, 198, 185, 62, 71, 252, 237, 124, 30, 158, 168, 163, 42, 31, 233, 82, 186, 143,
        81, 151, 96, 179, 7,
    ])
    .unwrap()
}

fn copy_dir(from: &Path, to: &Path) -> Result<()> {
    let walker = WalkDir::new(from);
    for entry in walker.into_iter() {
        let entry = entry?;
        let to_path = to.join(entry.path().strip_prefix(from)?);
        if entry.metadata()?.is_dir() {
            create_dir(&to_path).context(format!("creating {:?}", to_path))?;
        } else {
            copy(entry.path(), &to_path).context(format!(
                "copying {:?} to {:?}",
                entry.path(),
                to_path
            ))?;
        }
    }

    Ok(())
}

pub async fn make_readonly_empty_repository(name: &str) -> Result<Repository> {
    let backend = PmRepository::new(Utf8PathBuf::from(EMPTY_REPO_PATH));
    Repository::new(name, Box::new(backend)).await.map_err(|e| anyhow!(e))
}

pub async fn make_writable_empty_repository(name: &str, root: Utf8PathBuf) -> Result<Repository> {
    let src = PathBuf::from(EMPTY_REPO_PATH).canonicalize()?;
    copy_dir(&src, root.as_std_path())?;
    let backend = PmRepository::new(root);
    Ok(Repository::new(name, Box::new(backend)).await?)
}
