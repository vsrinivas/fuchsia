// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::RepoPublishCommand,
    anyhow::{Context, Result},
    fuchsia_pkg::{PackageManifest, PackageManifestList},
    fuchsia_repo::{
        repo_builder::RepoBuilder, repo_client::RepoClient, repo_keys::RepoKeys,
        repository::PmRepository,
    },
    std::fs::File,
};

pub async fn cmd_repo_publish(cmd: RepoPublishCommand) -> Result<()> {
    let repo = PmRepository::new(cmd.repo_path.clone());

    // Load the keys. If we weren't passed in a keys file, try to read it from the repository.
    let repo_keys = if let Some(repo_keys_path) = cmd.keys {
        RepoKeys::from_dir(repo_keys_path.as_std_path())?
    } else {
        repo.repo_keys()?
    };

    // Connect to the repository and make sure we have the latest version available.
    let mut client = RepoClient::from_trusted_remote(repo).await?;
    client.update().await?;

    // Load in all the package manifests up front so we'd error out if any are missing or malformed.
    let mut packages = vec![];
    for package_manifest_path in cmd.package_manifests {
        packages.push(
            PackageManifest::try_load_from(package_manifest_path.as_std_path())
                .with_context(|| format!("reading package manifest {}", package_manifest_path))?,
        );
    }

    for package_list_manifest_path in cmd.package_list_manifests {
        let file = File::open(package_list_manifest_path)?;

        for package_manifest_path in PackageManifestList::from_reader(file)? {
            packages.push(PackageManifest::try_load_from(package_manifest_path)?);
        }
    }

    // Publish all the packages.
    let mut repo_builder = RepoBuilder::from_client(&client, &repo_keys)
        .refresh_non_root_metadata(true)
        .time_versioning(cmd.time_versioning);

    for package in packages {
        repo_builder = repo_builder.add_package(package);
    }

    repo_builder.commit().await?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*, assert_matches::assert_matches, camino::Utf8Path, chrono::Utc,
        fuchsia_repo::test_utils, tuf::metadata::Metadata as _,
    };

    #[fuchsia::test]
    async fn test_repository_does_not_exist() {
        let tempdir = tempfile::tempdir().unwrap();
        let repo_path = Utf8Path::from_path(tempdir.path()).unwrap();

        let cmd = RepoPublishCommand {
            keys: None,
            package_manifests: vec![],
            package_list_manifests: vec![],
            time_versioning: false,
            repo_path: repo_path.to_path_buf(),
        };

        assert_matches!(cmd_repo_publish(cmd).await, Err(_));
    }

    #[fuchsia::test]
    async fn test_publish_nothing_to_empty_pm_repo() {
        let tempdir = tempfile::tempdir().unwrap();
        let repo_path = Utf8Path::from_path(tempdir.path()).unwrap();

        test_utils::make_repo_dir(repo_path).unwrap();

        // Connect to the repo before we run the command to make sure we generate new metadata.
        let repo = PmRepository::new(repo_path.to_path_buf());
        let mut repo_client = RepoClient::from_trusted_remote(repo).await.unwrap();

        assert_matches!(repo_client.update().await, Ok(true));
        assert_eq!(repo_client.database().trusted_root().version(), 1);
        assert_eq!(repo_client.database().trusted_targets().map(|m| m.version()), Some(1));
        assert_eq!(repo_client.database().trusted_snapshot().map(|m| m.version()), Some(1));
        assert_eq!(repo_client.database().trusted_timestamp().map(|m| m.version()), Some(1));

        let cmd = RepoPublishCommand {
            keys: None,
            package_manifests: vec![],
            package_list_manifests: vec![],
            time_versioning: false,
            repo_path: repo_path.to_path_buf(),
        };

        assert_matches!(cmd_repo_publish(cmd).await, Ok(()));

        // Even though we didn't add any packages, we still should have refreshed the metadata.
        assert_matches!(repo_client.update().await, Ok(true));
        assert_eq!(repo_client.database().trusted_root().version(), 1);
        assert_eq!(repo_client.database().trusted_targets().map(|m| m.version()), Some(2));
        assert_eq!(repo_client.database().trusted_snapshot().map(|m| m.version()), Some(2));
        assert_eq!(repo_client.database().trusted_timestamp().map(|m| m.version()), Some(2));
    }

    #[fuchsia::test]
    async fn test_keys_path() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = Utf8Path::from_path(tempdir.path()).unwrap();
        let repo_path = root.join("repository");
        let keys_path = root.join("keys");

        test_utils::make_repo_dir(&repo_path).unwrap();

        // Move the keys directory out of the repository. We should error out since we can't find
        // the keys.
        std::fs::rename(repo_path.join("keys"), &keys_path).unwrap();

        let cmd = RepoPublishCommand {
            keys: None,
            package_manifests: vec![],
            package_list_manifests: vec![],
            time_versioning: false,
            repo_path: repo_path.to_path_buf(),
        };

        assert_matches!(cmd_repo_publish(cmd).await, Err(_));

        // Explicitly specifying the keys path should work though.
        let cmd = RepoPublishCommand {
            keys: Some(keys_path),
            package_manifests: vec![],
            package_list_manifests: vec![],
            time_versioning: false,
            repo_path: repo_path.to_path_buf(),
        };

        assert_matches!(cmd_repo_publish(cmd).await, Ok(()));
    }

    #[fuchsia::test]
    async fn test_time_versioning() {
        let tempdir = tempfile::tempdir().unwrap();
        let repo_path = Utf8Path::from_path(tempdir.path()).unwrap();

        // Time versioning uses the current unix timestamp relative to UTC. Look up that number,
        // and we'll assert that the metadata version is >= to this number.
        let now = Utc::now().timestamp().try_into().unwrap();

        test_utils::make_repo_dir(repo_path).unwrap();

        let cmd = RepoPublishCommand {
            keys: None,
            package_manifests: vec![],
            package_list_manifests: vec![],
            time_versioning: true,
            repo_path: repo_path.to_path_buf(),
        };

        assert_matches!(cmd_repo_publish(cmd).await, Ok(()));

        // The metadata we generated should use the current time for a version.
        let repo = PmRepository::new(repo_path.to_path_buf());
        let mut repo_client = RepoClient::from_trusted_remote(repo).await.unwrap();

        assert_matches!(repo_client.update().await, Ok(true));
        assert_eq!(repo_client.database().trusted_root().version(), 1);
        assert!(repo_client.database().trusted_targets().map(|m| m.version()).unwrap() >= now);
        assert!(repo_client.database().trusted_snapshot().map(|m| m.version()).unwrap() >= now);
        assert!(repo_client.database().trusted_timestamp().map(|m| m.version()).unwrap() >= now);
    }

    #[fuchsia::test]
    async fn test_publish_packages() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = Utf8Path::from_path(tempdir.path()).unwrap();

        let repo_path = root.join("repo");
        test_utils::make_repo_dir(&repo_path).unwrap();

        // Build some packages to publish.
        let mut manifests = vec![];
        for name in ["package1", "package2", "package3", "package4", "package5"] {
            let pkg_build_path = root.join(name);
            let pkg_manifest_path = root.join(format!("{}.json", name));

            let (_, pkg_manifest) =
                test_utils::make_package_manifest(name, pkg_build_path.as_std_path());

            serde_json::to_writer(File::create(&pkg_manifest_path).unwrap(), &pkg_manifest)
                .unwrap();

            manifests.push(pkg_manifest);
        }

        let pkg1_manifest_path = root.join("package1.json");
        let pkg2_manifest_path = root.join("package2.json");

        // Bundle up package3, package4, and package5 into package list manifests.
        let pkg_list1_manifest =
            PackageManifestList::from(vec![root.join("package3.json"), root.join("package4.json")]);
        let pkg_list1_manifest_path = root.join("list1.json");
        pkg_list1_manifest.to_writer(File::create(&pkg_list1_manifest_path).unwrap()).unwrap();

        let pkg_list2_manifest = PackageManifestList::from(vec![root.join("package5.json")]);
        let pkg_list2_manifest_path = root.join("list2.json");
        pkg_list2_manifest.to_writer(File::create(&pkg_list2_manifest_path).unwrap()).unwrap();

        // Publish the packages.
        let cmd = RepoPublishCommand {
            keys: None,
            package_manifests: vec![pkg1_manifest_path, pkg2_manifest_path],
            package_list_manifests: vec![pkg_list1_manifest_path, pkg_list2_manifest_path],
            time_versioning: false,
            repo_path: repo_path.to_path_buf(),
        };

        assert_matches!(cmd_repo_publish(cmd).await, Ok(()));

        let repo = PmRepository::new(repo_path.to_path_buf());
        let mut repo_client = RepoClient::from_trusted_remote(repo).await.unwrap();

        assert_matches!(repo_client.update().await, Ok(true));

        assert_eq!(repo_client.database().trusted_root().version(), 1);
        assert_eq!(repo_client.database().trusted_targets().map(|m| m.version()), Some(2));
        assert_eq!(repo_client.database().trusted_snapshot().map(|m| m.version()), Some(2));
        assert_eq!(repo_client.database().trusted_timestamp().map(|m| m.version()), Some(2));

        let blob_repo_path = repo_path.join("repository").join("blobs");

        for package_manifest in manifests {
            for blob in package_manifest.blobs() {
                let blob_path = blob_repo_path.join(&blob.merkle.to_string());

                assert_eq!(
                    std::fs::read(&blob.source_path).unwrap(),
                    std::fs::read(&blob_path).unwrap(),
                );
            }
        }
    }
}
