// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::RepoPublishCommand,
    anyhow::Result,
    fuchsia_pkg::{PackageManifest, PackageManifestList},
    fuchsia_repo::{
        repo_builder::RepoBuilder, repo_client::RepoClient, repo_keys::RepoKeys,
        repository::PmRepository,
    },
    std::fs::File,
};

pub async fn cmd_repo_publish(cmd: RepoPublishCommand) -> Result<()> {
    let repo_keys_path = if let Some(keys) = cmd.keys { keys } else { cmd.repo_path.join("keys") };
    let repo_keys = RepoKeys::from_dir(repo_keys_path.as_std_path())?;

    let repo = PmRepository::new(cmd.repo_path);
    let mut client = RepoClient::from_trusted_remote(repo).await?;
    client.update().await?;

    let mut repo_builder =
        RepoBuilder::from_client(&client, &repo_keys).time_versioning(cmd.time_versioning);

    for package_manifest_path in cmd.package_manifests {
        let package = PackageManifest::try_load_from(package_manifest_path)?;
        repo_builder = repo_builder.add_package(package);
    }

    for package_list_manifest_path in cmd.package_list_manifests {
        let file = File::open(package_list_manifest_path)?;

        for package_manifest_path in PackageManifestList::from_reader(file)? {
            let package = PackageManifest::try_load_from(package_manifest_path)?;
            repo_builder = repo_builder.add_package(package);
        }
    }

    repo_builder.commit().await?;

    Ok(())
}
