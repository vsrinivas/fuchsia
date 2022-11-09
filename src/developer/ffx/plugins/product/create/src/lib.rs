// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! FFX plugin for constructing product bundles, which are distributable containers for a product's
//! images and packages, and can be used to emulate, flash, or update a product.

use anyhow::{Context, Result};
use assembly_manifest::{AssemblyManifest, BlobfsContents, Image, PackagesMetadata};
use assembly_partitions_config::PartitionsConfig;
use assembly_tool::{SdkToolProvider, ToolProvider};
use assembly_update_package::{Slot, UpdatePackageBuilder};
use assembly_update_packages_manifest::UpdatePackagesManifest;
use camino::{Utf8Path, Utf8PathBuf};
use epoch::EpochFile;
use ffx_core::ffx_plugin;
use ffx_product_create_args::CreateCommand;
use fuchsia_pkg::PackageManifest;
use fuchsia_repo::{
    repo_builder::RepoBuilder, repo_keys::RepoKeys, repository::FileSystemRepository,
};
use sdk_metadata::{ProductBundle, ProductBundleV2, Repository};
use std::fs::File;
use tempfile::TempDir;

/// Create a product bundle.
#[ffx_plugin("product.experimental")]
pub async fn pb_create(cmd: CreateCommand) -> Result<()> {
    let sdk_tools = SdkToolProvider::try_new().context("getting sdk tools")?;
    pb_create_with_tools(cmd, Box::new(sdk_tools)).await
}

/// Create a product bundle using the provided `tools`.
pub async fn pb_create_with_tools(cmd: CreateCommand, tools: Box<dyn ToolProvider>) -> Result<()> {
    // We build an update package if `update_version_file` or `update_epoch` is provided.
    // If we decide to build an update package, we need to ensure that both of them
    // are provided.
    let update_details =
        if cmd.update_package_version_file.is_some() || cmd.update_package_epoch.is_some() {
            if cmd.tuf_keys.is_none() {
                anyhow::bail!("TUF keys must be provided to build an update package");
            }
            let version = cmd.update_package_version_file.ok_or(anyhow::anyhow!(
                "A version file must be provided to build an update package"
            ))?;
            let epoch = cmd
                .update_package_epoch
                .ok_or(anyhow::anyhow!("A epoch must be provided to build an update package"))?;
            Some((version, epoch))
        } else {
            None
        };

    // Make sure `out_dir` is created and empty.
    if cmd.out_dir.exists() {
        std::fs::remove_dir_all(&cmd.out_dir).context("Deleting the out_dir")?;
    }
    std::fs::create_dir_all(&cmd.out_dir).context("Creating the out_dir")?;

    let partitions = load_partitions_config(&cmd.partitions, &cmd.out_dir.join("partitions"))?;
    let (system_a, packages_a) =
        load_assembly_manifest(&cmd.system_a, &cmd.out_dir.join("system_a"))?;
    let (system_b, packages_b) =
        load_assembly_manifest(&cmd.system_b, &cmd.out_dir.join("system_b"))?;
    let (system_r, _packages_r) =
        load_assembly_manifest(&cmd.system_r, &cmd.out_dir.join("system_r"))?;

    // Generate the update packages if necessary.
    let (_gen_dir, update_package_hash, update_packages) =
        if let Some((version, epoch)) = update_details {
            let epoch: EpochFile = EpochFile::Version1 { epoch };
            let abi_revision = None;
            let gen_dir = TempDir::new().context("creating temporary directory")?;
            let mut builder = UpdatePackageBuilder::new(
                tools,
                partitions.clone(),
                partitions.hardware_revision.clone(),
                version,
                epoch,
                abi_revision,
                Utf8Path::from_path(gen_dir.path())
                    .context("checkinf if temporary directory is UTF-8")?,
            );
            let mut all_packages = UpdatePackagesManifest::default();
            for package in &packages_a {
                all_packages.add_by_manifest(package.clone())?;
            }
            builder.add_packages(all_packages);
            if let Some(manifest) = &system_a {
                builder.add_slot_images(Slot::Primary(manifest.clone()));
            }
            if let Some(manifest) = &system_r {
                builder.add_slot_images(Slot::Recovery(manifest.clone()));
            }
            let update_package = builder.build()?;
            (Some(gen_dir), Some(update_package.merkle), update_package.package_manifests)
        } else {
            (None, None, vec![])
        };

    let repositories = if let Some(tuf_keys) = &cmd.tuf_keys {
        let repo_path = &cmd.out_dir;
        let metadata_path = repo_path.join("repository");
        let blobs_path = repo_path.join("blobs");
        let repo = FileSystemRepository::new(metadata_path.to_path_buf(), blobs_path.to_path_buf());
        let repo_keys =
            RepoKeys::from_dir(tuf_keys.as_std_path()).context("Gathering repo keys")?;

        RepoBuilder::create(&repo, &repo_keys)
            .add_packages(packages_a.into_iter())?
            .add_packages(packages_b.into_iter())?
            .add_packages(update_packages.into_iter())?
            .commit()
            .await
            .context("Building the repo")?;
        let name = "fuchsia.com".to_string();
        vec![Repository { name, metadata_path, blobs_path }]
    } else {
        vec![]
    };

    let product_bundle = ProductBundleV2 {
        partitions,
        system_a,
        system_b,
        system_r,
        repositories,
        update_package_hash,
    };
    let product_bundle = ProductBundle::V2(product_bundle);
    product_bundle.write(&cmd.out_dir).context("writing product bundle")?;
    Ok(())
}

/// Open and parse a PartitionsConfig from a path, copying the images into `out_dir`.
fn load_partitions_config(
    path: impl AsRef<Utf8Path>,
    out_dir: impl AsRef<Utf8Path>,
) -> Result<PartitionsConfig> {
    let path = path.as_ref();
    let out_dir = out_dir.as_ref();

    // Make sure `out_dir` is created.
    std::fs::create_dir_all(&out_dir).context("Creating the out_dir")?;

    let partitions_file = File::open(path).context("Opening partitions config")?;
    let mut config: PartitionsConfig =
        serde_json::from_reader(partitions_file).context("Parsing partitions config")?;

    for cred in &mut config.unlock_credentials {
        *cred = copy_file(&cred, &out_dir)?;
    }
    for bootstrap in &mut config.bootstrap_partitions {
        bootstrap.image = copy_file(&bootstrap.image, &out_dir)?;
    }
    for bootloader in &mut config.bootloader_partitions {
        bootloader.image = copy_file(&bootloader.image, &out_dir)?;
    }

    Ok(config)
}

/// Open and parse an AssemblyManifest from a path, copying the images into `out_dir`.
/// Returns None if the given path is None.
fn load_assembly_manifest(
    path: &Option<Utf8PathBuf>,
    out_dir: impl AsRef<Utf8Path>,
) -> Result<(Option<AssemblyManifest>, Vec<PackageManifest>)> {
    let out_dir = out_dir.as_ref();

    if let Some(path) = path {
        // Make sure `out_dir` is created.
        std::fs::create_dir_all(&out_dir).context("Creating the out_dir")?;

        let file =
            File::open(path).with_context(|| format!("Opening assembly manifest: {}", path))?;
        let manifest: AssemblyManifest = serde_json::from_reader(file)
            .with_context(|| format!("Parsing assembly manifest: {}", path))?;

        // Filter out the base package, and the blobfs contents.
        let mut images = Vec::<Image>::new();
        let mut packages = Vec::<PackageManifest>::new();
        for image in manifest.images.into_iter() {
            match image {
                Image::BasePackage(..) => {}
                Image::BlobFS { path, contents } => {
                    let PackagesMetadata { base, cache } = contents.packages;
                    let all_packages = [base.0, cache.0].concat();
                    for package in all_packages {
                        let manifest = PackageManifest::try_load_from(&package.manifest)
                            .with_context(|| {
                                format!("reading package manifest: {}", package.manifest)
                            })?;
                        packages.push(manifest);
                    }
                    images.push(Image::BlobFS { path, contents: BlobfsContents::default() });
                }
                _ => {
                    images.push(image);
                }
            }
        }

        // Copy the images to the `out_dir`.
        let mut new_images = Vec::<Image>::new();
        for mut image in images.into_iter() {
            let dest = copy_file(image.source(), &out_dir)?;
            image.set_source(dest);
            new_images.push(image);
        }

        Ok((Some(AssemblyManifest { images: new_images }), packages))
    } else {
        Ok((None, vec![]))
    }
}

/// Copy a file from `source` to `out_dir` preserving the filename.
/// Returns the destination, which is equal to {out_dir}{filename}.
fn copy_file(source: impl AsRef<Utf8Path>, out_dir: impl AsRef<Utf8Path>) -> Result<Utf8PathBuf> {
    let source = source.as_ref();
    let out_dir = out_dir.as_ref();
    let filename = source.file_name().context("getting file name")?;
    let destination = out_dir.join(filename);
    std::fs::copy(source, &destination).context("copying file")?;
    Ok(destination)
}

#[cfg(test)]
mod test {
    use super::*;
    use assembly_manifest::AssemblyManifest;
    use assembly_partitions_config::PartitionsConfig;
    use assembly_tool::testing::FakeToolProvider;
    use fuchsia_repo::test_utils;
    use std::io::Write;
    use tempfile::TempDir;

    #[test]
    fn test_copy_file() {
        let temp1 = TempDir::new().unwrap();
        let tempdir1 = Utf8Path::from_path(temp1.path()).unwrap();
        let temp2 = TempDir::new().unwrap();
        let tempdir2 = Utf8Path::from_path(temp2.path()).unwrap();

        let source_path = tempdir1.join("source.txt");
        let mut source_file = File::create(&source_path).unwrap();
        write!(source_file, "contents").unwrap();
        let destination = copy_file(&source_path, tempdir2).unwrap();
        assert!(destination.exists());
    }

    #[test]
    fn test_load_partitions_config() {
        let temp = TempDir::new().unwrap();
        let tempdir = Utf8Path::from_path(temp.path()).unwrap();
        let pb_dir = tempdir.join("pb");

        let config_path = tempdir.join("config.json");
        let config_file = File::create(&config_path).unwrap();
        serde_json::to_writer(&config_file, &PartitionsConfig::default()).unwrap();

        let error_path = tempdir.join("error.json");
        let mut error_file = File::create(&error_path).unwrap();
        error_file.write_all("error".as_bytes()).unwrap();

        let parsed = load_partitions_config(&config_path, &pb_dir);
        assert!(parsed.is_ok());

        let error = load_partitions_config(&error_path, &pb_dir);
        assert!(error.is_err());
    }

    #[test]
    fn test_load_assembly_manifest() {
        let temp = TempDir::new().unwrap();
        let tempdir = Utf8Path::from_path(temp.path()).unwrap();
        let pb_dir = tempdir.join("pb");

        let manifest_path = tempdir.join("manifest.json");
        let manifest_file = File::create(&manifest_path).unwrap();
        serde_json::to_writer(&manifest_file, &AssemblyManifest::default()).unwrap();

        let error_path = tempdir.join("error.json");
        let mut error_file = File::create(&error_path).unwrap();
        error_file.write_all("error".as_bytes()).unwrap();

        let (parsed, packages) = load_assembly_manifest(&Some(manifest_path), &pb_dir).unwrap();
        assert!(parsed.is_some());
        assert_eq!(packages, Vec::<PackageManifest>::new());

        let error = load_assembly_manifest(&Some(error_path), &pb_dir);
        assert!(error.is_err());

        let (none, _) = load_assembly_manifest(&None, &pb_dir).unwrap();
        assert!(none.is_none());
    }

    #[fuchsia::test]
    async fn test_pb_create_minimal() {
        let temp = TempDir::new().unwrap();
        let tempdir = Utf8Path::from_path(temp.path()).unwrap();
        let pb_dir = tempdir.join("pb");

        let partitions_path = tempdir.join("partitions.json");
        let partitions_file = File::create(&partitions_path).unwrap();
        serde_json::to_writer(&partitions_file, &PartitionsConfig::default()).unwrap();

        let tools = FakeToolProvider::default();
        pb_create_with_tools(
            CreateCommand {
                partitions: partitions_path,
                system_a: None,
                system_b: None,
                system_r: None,
                tuf_keys: None,
                update_package_version_file: None,
                update_package_epoch: None,
                out_dir: pb_dir.clone(),
            },
            Box::new(tools),
        )
        .await
        .unwrap();

        let pb = ProductBundle::try_load_from(pb_dir).unwrap();
        assert_eq!(
            pb,
            ProductBundle::V2(ProductBundleV2 {
                partitions: PartitionsConfig::default(),
                system_a: None,
                system_b: None,
                system_r: None,
                repositories: vec![],
                update_package_hash: None,
            })
        );
    }

    #[fuchsia::test]
    async fn test_pb_create_a_and_r() {
        let temp = TempDir::new().unwrap();
        let tempdir = Utf8Path::from_path(temp.path()).unwrap();
        let pb_dir = tempdir.join("pb");

        let partitions_path = tempdir.join("partitions.json");
        let partitions_file = File::create(&partitions_path).unwrap();
        serde_json::to_writer(&partitions_file, &PartitionsConfig::default()).unwrap();

        let system_path = tempdir.join("system.json");
        let system_file = File::create(&system_path).unwrap();
        serde_json::to_writer(&system_file, &AssemblyManifest::default()).unwrap();

        let tools = FakeToolProvider::default();
        pb_create_with_tools(
            CreateCommand {
                partitions: partitions_path,
                system_a: Some(system_path.clone()),
                system_b: None,
                system_r: Some(system_path.clone()),
                tuf_keys: None,
                update_package_version_file: None,
                update_package_epoch: None,
                out_dir: pb_dir.clone(),
            },
            Box::new(tools),
        )
        .await
        .unwrap();

        let pb = ProductBundle::try_load_from(pb_dir).unwrap();
        assert_eq!(
            pb,
            ProductBundle::V2(ProductBundleV2 {
                partitions: PartitionsConfig::default(),
                system_a: Some(AssemblyManifest::default()),
                system_b: None,
                system_r: Some(AssemblyManifest::default()),
                repositories: vec![],
                update_package_hash: None,
            })
        );
    }

    #[fuchsia::test]
    async fn test_pb_create_a_and_r_and_repository() {
        let temp = TempDir::new().unwrap();
        let tempdir = Utf8Path::from_path(temp.path()).unwrap();
        let pb_dir = tempdir.join("pb");

        let partitions_path = tempdir.join("partitions.json");
        let partitions_file = File::create(&partitions_path).unwrap();
        serde_json::to_writer(&partitions_file, &PartitionsConfig::default()).unwrap();

        let system_path = tempdir.join("system.json");
        let system_file = File::create(&system_path).unwrap();
        serde_json::to_writer(&system_file, &AssemblyManifest::default()).unwrap();

        let tuf_keys = tempdir.join("keys");
        test_utils::make_repo_keys_dir(&tuf_keys);

        let tools = FakeToolProvider::default();
        pb_create_with_tools(
            CreateCommand {
                partitions: partitions_path,
                system_a: Some(system_path.clone()),
                system_b: None,
                system_r: Some(system_path.clone()),
                tuf_keys: Some(tuf_keys),
                update_package_version_file: None,
                update_package_epoch: None,
                out_dir: pb_dir.clone(),
            },
            Box::new(tools),
        )
        .await
        .unwrap();

        let pb = ProductBundle::try_load_from(&pb_dir).unwrap();
        assert_eq!(
            pb,
            ProductBundle::V2(ProductBundleV2 {
                partitions: PartitionsConfig::default(),
                system_a: Some(AssemblyManifest::default()),
                system_b: None,
                system_r: Some(AssemblyManifest::default()),
                repositories: vec![Repository {
                    name: "fuchsia.com".into(),
                    metadata_path: pb_dir.join("repository"),
                    blobs_path: pb_dir.join("blobs"),
                }],
                update_package_hash: None,
            })
        );
    }

    #[fuchsia::test]
    async fn test_pb_create_with_update() {
        let tmp = TempDir::new().unwrap();
        let tempdir = Utf8Path::from_path(tmp.path()).unwrap();

        let pb_dir = tempdir.join("pb");

        let partitions_path = tempdir.join("partitions.json");
        let partitions_file = File::create(&partitions_path).unwrap();
        serde_json::to_writer(&partitions_file, &PartitionsConfig::default()).unwrap();

        let version_path = tempdir.join("version.txt");
        std::fs::write(&version_path, "").unwrap();

        let tuf_keys = tempdir.join("keys");
        test_utils::make_repo_keys_dir(&tuf_keys);

        let tools = FakeToolProvider::default();
        pb_create_with_tools(
            CreateCommand {
                partitions: partitions_path,
                system_a: None,
                system_b: None,
                system_r: None,
                tuf_keys: Some(tuf_keys),
                update_package_version_file: Some(version_path),
                update_package_epoch: Some(1),
                out_dir: pb_dir.clone(),
            },
            Box::new(tools),
        )
        .await
        .unwrap();

        let expected_hash =
            "502806d5763dbb6500983667e8e6466e9efd291a6080372c03570d7372dfbab0".parse().unwrap();
        let pb = ProductBundle::try_load_from(&pb_dir).unwrap();
        assert_eq!(
            pb,
            ProductBundle::V2(ProductBundleV2 {
                partitions: PartitionsConfig::default(),
                system_a: None,
                system_b: None,
                system_r: None,
                repositories: vec![Repository {
                    name: "fuchsia.com".into(),
                    metadata_path: pb_dir.join("repository"),
                    blobs_path: pb_dir.join("blobs"),
                }],
                update_package_hash: Some(expected_hash),
            })
        );
    }
}
