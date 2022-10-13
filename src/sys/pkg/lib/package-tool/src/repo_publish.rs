// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::RepoPublishCommand,
    anyhow::{Context, Result},
    fuchsia_pkg::{PackageManifest, PackageManifestList},
    fuchsia_repo::{
        repo_builder::RepoBuilder,
        repo_client::RepoClient,
        repo_keys::RepoKeys,
        repository::{Error as RepoError, PmRepository},
    },
    std::{
        fs::File,
        io::{BufWriter, Write},
    },
    tuf::Error as TufError,
};

pub async fn cmd_repo_publish(cmd: RepoPublishCommand) -> Result<()> {
    let repo = PmRepository::builder(cmd.repo_path.clone()).copy_mode(cmd.copy_mode).build();

    // Load the signing metadata keys if from a file if specified.
    let repo_signing_keys = if let Some(path) = cmd.signing_keys {
        if !path.exists() {
            anyhow::bail!("--signing-keys path {} does not exist", path);
        }

        Some(RepoKeys::from_dir(path.as_std_path())?)
    } else {
        None
    };

    // Load the trusted metadata keys. If they weren't passed in a trusted keys file, try to read
    // the keys from the repository.
    let repo_trusted_keys = if let Some(path) = cmd.trusted_keys {
        if !path.exists() {
            anyhow::bail!("--trusted-keys path {} does not exist", path);
        }

        RepoKeys::from_dir(path.as_std_path())?
    } else {
        repo.repo_keys()?
    };

    // Load in all the package manifests up front so we'd error out if any are missing or malformed.
    let mut deps = vec![];
    let mut packages = vec![];
    for package_manifest_path in cmd.package_manifests {
        packages.push(
            PackageManifest::try_load_from(package_manifest_path.as_std_path())
                .with_context(|| format!("reading package manifest {}", package_manifest_path))?,
        );

        deps.push(package_manifest_path);
    }

    for package_list_manifest_path in cmd.package_list_manifests {
        let file = File::open(&package_list_manifest_path)?;

        for package_manifest_path in PackageManifestList::from_reader(file)? {
            packages.push(PackageManifest::try_load_from(package_manifest_path)?);
        }

        deps.push(package_list_manifest_path);
    }

    // Try to connect to the repository. This should succeed if we have at least some root metadata
    // in the repository. If none exists, we'll create a new repository.
    let client = match RepoClient::from_trusted_remote(&repo).await {
        Ok(mut client) => {
            // Make sure our client has the latest metadata. It's okay if this fails with missing
            // metadata since we'll create it when we publish to the repository.
            match client.update().await {
                Ok(_) | Err(RepoError::Tuf(TufError::MetadataNotFound { .. })) => {}
                Err(err) => {
                    return Err(err.into());
                }
            }

            Some(client)
        }
        Err(RepoError::Tuf(TufError::MetadataNotFound { .. })) => None,
        Err(err) => {
            return Err(err.into());
        }
    };

    let mut repo_builder = if let Some(client) = &client {
        RepoBuilder::from_database(&repo, &repo_trusted_keys, client.database())
    } else {
        RepoBuilder::create(&repo, &repo_trusted_keys)
    };

    if let Some(repo_signing_keys) = &repo_signing_keys {
        repo_builder = repo_builder.signing_repo_keys(&repo_signing_keys);
    }

    repo_builder = repo_builder
        .refresh_non_root_metadata(true)
        .time_versioning(cmd.time_versioning)
        .inherit_from_trusted_targets(!cmd.clean);

    // Publish all the packages.
    for package in packages {
        repo_builder = repo_builder.add_package(package);
    }

    repo_builder.commit().await?;

    if let Some(depfile_path) = cmd.depfile {
        let timestamp_path = cmd.repo_path.join("repository").join("timestamp.json");

        let file =
            File::create(&depfile_path).with_context(|| format!("creating {}", depfile_path))?;

        let mut file = BufWriter::new(file);

        write!(file, "{}:", timestamp_path)?;

        for path in deps {
            // Spaces are separators, so spaces in filenames must be escaped.
            let path = path.as_str().replace(' ', "\\ ");

            write!(file, " {}", path)?;
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        camino::Utf8Path,
        chrono::Utc,
        fuchsia_repo::{repository::CopyMode, test_utils},
        tuf::metadata::Metadata as _,
    };

    #[fuchsia::test]
    async fn test_repository_should_error_with_no_keys_if_it_does_not_exist() {
        let tempdir = tempfile::tempdir().unwrap();
        let repo_path = Utf8Path::from_path(tempdir.path()).unwrap();

        let cmd = RepoPublishCommand {
            signing_keys: None,
            trusted_keys: None,
            package_manifests: vec![],
            package_list_manifests: vec![],
            time_versioning: false,
            clean: false,
            depfile: None,
            copy_mode: CopyMode::Copy,
            repo_path: repo_path.to_path_buf(),
        };

        assert_matches!(cmd_repo_publish(cmd).await, Err(_));
    }

    #[fuchsia::test]
    async fn test_repository_should_create_repo_with_keys_if_it_does_not_exist() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = Utf8Path::from_path(tempdir.path()).unwrap();

        let repo_keys_path = root.join("keys");
        let repo_path = root.join("repo");

        test_utils::make_repo_keys_dir(&repo_keys_path);

        let cmd = RepoPublishCommand {
            signing_keys: None,
            trusted_keys: Some(repo_keys_path),
            package_manifests: vec![],
            package_list_manifests: vec![],
            time_versioning: false,
            clean: false,
            depfile: None,
            copy_mode: CopyMode::Copy,
            repo_path: repo_path.to_path_buf(),
        };

        assert_matches!(cmd_repo_publish(cmd).await, Ok(()));

        let repo = PmRepository::new(repo_path);
        let mut repo_client = RepoClient::from_trusted_remote(repo).await.unwrap();

        assert_matches!(repo_client.update().await, Ok(true));
        assert_eq!(repo_client.database().trusted_root().version(), 1);
        assert_eq!(repo_client.database().trusted_targets().map(|m| m.version()), Some(1));
        assert_eq!(repo_client.database().trusted_snapshot().map(|m| m.version()), Some(1));
        assert_eq!(repo_client.database().trusted_timestamp().map(|m| m.version()), Some(1));
    }

    #[fuchsia::test]
    async fn test_repository_should_create_repo_if_only_root() {
        let tmp = tempfile::tempdir().unwrap();
        let root = Utf8Path::from_path(tmp.path()).unwrap();

        // First create a repository.
        let full_repo_path = root.join("full");
        let full_metadata_repo_path = full_repo_path.join("repository");
        test_utils::make_pm_repo_dir(full_repo_path.as_std_path()).await;

        // Then create a repository, which only has the keys and root metadata in it.
        let test_repo_path = root.join("test");
        let test_metadata_repo_path = test_repo_path.join("repository");
        std::fs::create_dir_all(&test_metadata_repo_path).unwrap();

        std::fs::rename(full_repo_path.join("keys"), test_repo_path.join("keys")).unwrap();

        std::fs::copy(
            full_metadata_repo_path.join("root.json"),
            test_metadata_repo_path.join("1.root.json"),
        )
        .unwrap();

        let cmd = RepoPublishCommand {
            signing_keys: None,
            trusted_keys: None,
            package_manifests: vec![],
            package_list_manifests: vec![],
            time_versioning: false,
            clean: false,
            depfile: None,
            copy_mode: CopyMode::Copy,
            repo_path: test_repo_path.to_path_buf(),
        };

        assert_matches!(cmd_repo_publish(cmd).await, Ok(()));

        let repo = PmRepository::new(test_repo_path);
        let mut repo_client = RepoClient::from_trusted_remote(repo).await.unwrap();

        assert_matches!(repo_client.update().await, Ok(true));
        assert_eq!(repo_client.database().trusted_root().version(), 1);
        assert_eq!(repo_client.database().trusted_targets().map(|m| m.version()), Some(1));
        assert_eq!(repo_client.database().trusted_snapshot().map(|m| m.version()), Some(1));
        assert_eq!(repo_client.database().trusted_timestamp().map(|m| m.version()), Some(1));
    }

    #[fuchsia::test]
    async fn test_publish_nothing_to_empty_pm_repo() {
        let tempdir = tempfile::tempdir().unwrap();
        let repo_path = Utf8Path::from_path(tempdir.path()).unwrap();

        test_utils::make_empty_pm_repo_dir(repo_path);

        // Connect to the repo before we run the command to make sure we generate new metadata.
        let repo = PmRepository::new(repo_path.to_path_buf());
        let mut repo_client = RepoClient::from_trusted_remote(repo).await.unwrap();

        assert_matches!(repo_client.update().await, Ok(true));
        assert_eq!(repo_client.database().trusted_root().version(), 1);
        assert_eq!(repo_client.database().trusted_targets().map(|m| m.version()), Some(1));
        assert_eq!(repo_client.database().trusted_snapshot().map(|m| m.version()), Some(1));
        assert_eq!(repo_client.database().trusted_timestamp().map(|m| m.version()), Some(1));

        let cmd = RepoPublishCommand {
            signing_keys: None,
            trusted_keys: None,
            package_manifests: vec![],
            package_list_manifests: vec![],
            time_versioning: false,
            clean: false,
            depfile: None,
            copy_mode: CopyMode::Copy,

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

        test_utils::make_empty_pm_repo_dir(&repo_path);

        // Move the keys directory out of the repository. We should error out since we can't find
        // the keys.
        std::fs::rename(repo_path.join("keys"), &keys_path).unwrap();

        let cmd = RepoPublishCommand {
            signing_keys: None,
            trusted_keys: None,
            package_manifests: vec![],
            package_list_manifests: vec![],
            time_versioning: false,
            clean: false,
            depfile: None,
            copy_mode: CopyMode::Copy,
            repo_path: repo_path.to_path_buf(),
        };

        assert_matches!(cmd_repo_publish(cmd).await, Err(_));

        // Explicitly specifying the keys path should work though.
        let cmd = RepoPublishCommand {
            signing_keys: None,
            trusted_keys: Some(keys_path),
            package_manifests: vec![],
            package_list_manifests: vec![],
            time_versioning: false,
            clean: false,
            depfile: None,
            copy_mode: CopyMode::Copy,
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

        test_utils::make_empty_pm_repo_dir(repo_path);

        let cmd = RepoPublishCommand {
            signing_keys: None,
            trusted_keys: None,
            package_manifests: vec![],
            package_list_manifests: vec![],
            time_versioning: true,
            clean: false,
            depfile: None,
            copy_mode: CopyMode::Copy,
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
        test_utils::make_empty_pm_repo_dir(&repo_path);

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

        let depfile_path = root.join("deps");

        // Publish the packages.
        let cmd = RepoPublishCommand {
            signing_keys: None,
            trusted_keys: None,
            package_manifests: vec![pkg1_manifest_path.clone(), pkg2_manifest_path.clone()],
            package_list_manifests: vec![
                pkg_list1_manifest_path.clone(),
                pkg_list2_manifest_path.clone(),
            ],
            time_versioning: false,
            clean: false,
            depfile: Some(depfile_path.clone()),
            copy_mode: CopyMode::Copy,
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

        assert_eq!(
            std::fs::read_to_string(&depfile_path).unwrap(),
            format!(
                "{}: {} {} {} {}",
                repo_path.join("repository").join("timestamp.json"),
                pkg1_manifest_path,
                pkg2_manifest_path,
                pkg_list1_manifest_path,
                pkg_list2_manifest_path
            )
        );
    }

    #[fuchsia::test]
    async fn test_publish_packages_should_support_cleaning() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = Utf8Path::from_path(tempdir.path()).unwrap();

        // Create a repository that contains packages package1 and package2.
        let repo_path = root.join("repo");
        test_utils::make_pm_repo_dir(repo_path.as_std_path()).await;

        // Publish package3 without cleaning enabled. This should preserve the old packages.
        let pkg3_manifest_path = root.join("package3.json");
        let (_, pkg3_manifest) =
            test_utils::make_package_manifest("package3", root.join("pkg3").as_std_path());
        serde_json::to_writer(File::create(&pkg3_manifest_path).unwrap(), &pkg3_manifest).unwrap();

        let cmd = RepoPublishCommand {
            signing_keys: None,
            trusted_keys: None,
            package_manifests: vec![pkg3_manifest_path],
            package_list_manifests: vec![],
            time_versioning: false,
            clean: false,
            depfile: None,
            copy_mode: CopyMode::Copy,
            repo_path: repo_path.to_path_buf(),
        };
        assert_matches!(cmd_repo_publish(cmd).await, Ok(()));

        let repo = PmRepository::new(repo_path.to_path_buf());
        let mut repo_client = RepoClient::from_trusted_remote(repo).await.unwrap();

        assert_matches!(repo_client.update().await, Ok(true));
        let trusted_targets = repo_client.database().trusted_targets().unwrap();
        assert!(trusted_targets.targets().get("package1/0").is_some());
        assert!(trusted_targets.targets().get("package2/0").is_some());
        assert!(trusted_targets.targets().get("package3/0").is_some());

        // Publish package4 with cleaning should clean out the old packages.
        let pkg4_manifest_path = root.join("package4.json");
        let (_, pkg4_manifest) =
            test_utils::make_package_manifest("package4", root.join("pkg4").as_std_path());
        serde_json::to_writer(File::create(&pkg4_manifest_path).unwrap(), &pkg4_manifest).unwrap();

        let cmd = RepoPublishCommand {
            signing_keys: None,
            trusted_keys: None,
            package_manifests: vec![pkg4_manifest_path],
            package_list_manifests: vec![],
            time_versioning: false,
            clean: true,
            depfile: None,
            copy_mode: CopyMode::Copy,
            repo_path: repo_path.to_path_buf(),
        };
        assert_matches!(cmd_repo_publish(cmd).await, Ok(()));

        let repo = PmRepository::new(repo_path.to_path_buf());
        let mut repo_client = RepoClient::from_trusted_remote(repo).await.unwrap();

        assert_matches!(repo_client.update().await, Ok(true));
        let trusted_targets = repo_client.database().trusted_targets().unwrap();
        assert!(trusted_targets.targets().get("package1/0").is_none());
        assert!(trusted_targets.targets().get("package2/0").is_none());
        assert!(trusted_targets.targets().get("package3/0").is_none());
        assert!(trusted_targets.targets().get("package4/0").is_some());
    }
}
