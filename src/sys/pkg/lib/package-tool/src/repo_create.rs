// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::RepoCreateCommand,
    anyhow::Result,
    fuchsia_repo::{repo_builder::RepoBuilder, repo_keys::RepoKeys, repository::PmRepository},
};

pub async fn cmd_repo_create(cmd: RepoCreateCommand) -> Result<()> {
    let repo_keys = RepoKeys::from_dir(&cmd.keys)?;
    let repo = PmRepository::new(cmd.repo_path);

    RepoBuilder::create(repo, &repo_keys).time_versioning(cmd.time_versioning).commit().await?;

    Ok(())
}
