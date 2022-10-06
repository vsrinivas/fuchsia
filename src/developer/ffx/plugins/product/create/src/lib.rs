// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! FFX plugin for constructing product bundles, which are distributable containers for a product's
//! images and packages, and can be used to emulate, flash, or update a product.

use anyhow::{Context, Result};
use assembly_manifest::{AssemblyManifest, BlobfsContents, Image};
use assembly_partitions_config::PartitionsConfig;
use ffx_core::ffx_plugin;
use ffx_product_create_args::CreateCommand;
use pathdiff::diff_paths;
use sdk_metadata::{ProductBundle, ProductBundleV2};
use serde::Serialize;
use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};
use tempfile::NamedTempFile;

#[derive(Serialize, Debug)]
#[serde(tag = "version")]
enum UploadManifest {
    #[serde(rename = "1")]
    V1(UploadManifestV1),
}

#[derive(Serialize, Debug, Default)]
struct UploadManifestV1 {
    entries: Vec<UploadEntry>,
}

#[derive(Serialize, Debug, Default)]
struct UploadEntry {
    source: PathBuf,
    destination: PathBuf,
}

/// Create a product bundle.
#[ffx_plugin("product.experimental")]
fn pb_create(cmd: CreateCommand) -> Result<()> {
    // Make sure `out_dir` is created and empty.
    if cmd.out_dir.exists() {
        std::fs::remove_dir_all(&cmd.out_dir).context("Deleting the out_dir")?;
    }
    std::fs::create_dir_all(&cmd.out_dir).context("Creating the out_dir")?;

    let product_bundle = ProductBundleV2 {
        partitions: load_partitions_config(&cmd.partitions, &cmd.out_dir.join("partitions"))?,
        system_a: load_assembly_manifest(&cmd.system_a, &cmd.out_dir.join("system_a"))?,
        system_b: load_assembly_manifest(&cmd.system_b, &cmd.out_dir.join("system_b"))?,
        system_r: load_assembly_manifest(&cmd.system_r, &cmd.out_dir.join("system_r"))?,
    };

    if cmd.include_upload_manifest {
        let upload_manifest_file =
            NamedTempFile::new_in(&cmd.out_dir).context("Creating upload manifest file")?;
        write_upload_manifest(&cmd.out_dir, &product_bundle, &upload_manifest_file)?;
        upload_manifest_file
            .persist(&cmd.out_dir.join("upload.json"))
            .context("Persisting upload manifest file")?;
    }

    let product_bundle = ProductBundle::V2(product_bundle);
    product_bundle.write(&cmd.out_dir).context("writing product bundle")?;
    Ok(())
}

fn write_upload_manifest<W: Write>(
    out_dir: impl AsRef<Path>,
    product_bundle: &ProductBundleV2,
    writer: W,
) -> Result<()> {
    let mut upload_manifest = UploadManifestV1::default();
    upload_manifest.entries.push(UploadEntry {
        source: out_dir.as_ref().join("product_bundle.json"),
        destination: "product_bundle.json".into(),
    });

    // Add the images from the partitions config.
    for partition in &product_bundle.partitions.bootstrap_partitions {
        upload_manifest.entries.push(UploadEntry {
            source: partition.image.clone(),
            destination: diff_paths(&partition.image, &out_dir)
                .context("rebasing bootstrap image")?,
        });
    }
    for partition in &product_bundle.partitions.bootloader_partitions {
        upload_manifest.entries.push(UploadEntry {
            source: partition.image.clone(),
            destination: diff_paths(&partition.image, &out_dir)
                .context("rebasing bootloader image")?,
        });
    }
    for credential in &product_bundle.partitions.unlock_credentials {
        upload_manifest.entries.push(UploadEntry {
            source: credential.clone(),
            destination: diff_paths(credential, &out_dir).context("rebasing unlock credential")?,
        });
    }

    // Add the images from the systems.
    let mut upload_system = |system: &Option<AssemblyManifest>| -> Result<()> {
        if let Some(system) = system {
            for image in &system.images {
                upload_manifest.entries.push(UploadEntry {
                    source: image.source().clone(),
                    destination: diff_paths(image.source(), &out_dir)
                        .context("rebasing system image")?,
                });
            }
        }
        Ok(())
    };
    upload_system(&product_bundle.system_a)?;
    upload_system(&product_bundle.system_b)?;
    upload_system(&product_bundle.system_r)?;

    let upload_manifest = UploadManifest::V1(upload_manifest);
    serde_json::to_writer(writer, &upload_manifest).context("Writing upload manifest")
}

/// Open and parse a PartitionsConfig from a path, copying the images into `out_dir`.
fn load_partitions_config(
    path: impl AsRef<Path>,
    out_dir: impl AsRef<Path>,
) -> Result<PartitionsConfig> {
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
    path: &Option<PathBuf>,
    out_dir: impl AsRef<Path>,
) -> Result<Option<AssemblyManifest>> {
    if let Some(path) = path {
        // Make sure `out_dir` is created.
        std::fs::create_dir_all(&out_dir).context("Creating the out_dir")?;

        let file = File::open(path)
            .with_context(|| format!("Opening assembly manifest: {}", path.display()))?;
        let manifest: AssemblyManifest = serde_json::from_reader(file)
            .with_context(|| format!("Parsing assembly manifest: {}", path.display()))?;

        // Filter out the base package, and the blobfs contents.
        let images: Vec<Image> = manifest
            .images
            .into_iter()
            .filter_map(|i| match i {
                Image::BasePackage(..) => None,
                Image::BlobFS { path, contents: _ } => {
                    Some(Image::BlobFS { path, contents: BlobfsContents::default() })
                }
                _ => Some(i),
            })
            .collect();

        // Copy the images to the `out_dir`.
        let mut new_images = Vec::<Image>::new();
        for mut image in images.into_iter() {
            let dest = copy_file(image.source(), &out_dir)?;
            image.set_source(dest);
            new_images.push(image);
        }

        Ok(Some(AssemblyManifest { images: new_images }))
    } else {
        Ok(None)
    }
}

/// Copy a file from `source` to `out_dir` preserving the filename.
/// Returns the destination, which is equal to {out_dir}{filename}.
fn copy_file(source: impl AsRef<Path>, out_dir: impl AsRef<Path>) -> Result<PathBuf> {
    let filename = source.as_ref().file_name().context("getting file name")?;
    let destination = out_dir.as_ref().join(filename);
    std::fs::copy(source, &destination).context("copying file")?;
    Ok(destination)
}

#[cfg(test)]
mod test {
    use super::*;
    use assembly_manifest::AssemblyManifest;
    use assembly_partitions_config::{BootloaderPartition, BootstrapPartition, PartitionsConfig};
    use assembly_util::PathToStringExt;
    use std::io::Write;
    use tempfile::TempDir;

    #[test]
    fn test_copy_file() {
        let tempdir1 = TempDir::new().unwrap();
        let tempdir2 = TempDir::new().unwrap();
        let source_path = tempdir1.path().join("source.txt");
        let mut source_file = File::create(&source_path).unwrap();
        write!(source_file, "contents").unwrap();
        let destination = copy_file(&source_path, tempdir2.path()).unwrap();
        assert!(destination.exists());
    }

    #[test]
    fn test_load_partitions_config() {
        let tempdir = TempDir::new().unwrap();
        let pb_dir = tempdir.path().join("pb");

        let config_path = tempdir.path().join("config.json");
        let config_file = File::create(&config_path).unwrap();
        serde_json::to_writer(&config_file, &PartitionsConfig::default()).unwrap();

        let error_path = tempdir.path().join("error.json");
        let mut error_file = File::create(&error_path).unwrap();
        error_file.write_all("error".as_bytes()).unwrap();

        let parsed = load_partitions_config(&config_path, &pb_dir);
        assert!(parsed.is_ok());

        let error = load_partitions_config(&error_path, &pb_dir);
        assert!(error.is_err());
    }

    #[test]
    fn test_load_assembly_manifest() {
        let tempdir = TempDir::new().unwrap();
        let pb_dir = tempdir.path().join("pb");

        let manifest_path = tempdir.path().join("manifest.json");
        let manifest_file = File::create(&manifest_path).unwrap();
        serde_json::to_writer(&manifest_file, &AssemblyManifest::default()).unwrap();

        let error_path = tempdir.path().join("error.json");
        let mut error_file = File::create(&error_path).unwrap();
        error_file.write_all("error".as_bytes()).unwrap();

        let parsed = load_assembly_manifest(&Some(manifest_path), &pb_dir).unwrap();
        assert!(parsed.is_some());

        let error = load_assembly_manifest(&Some(error_path), &pb_dir);
        assert!(error.is_err());

        let none = load_assembly_manifest(&None, &pb_dir).unwrap();
        assert!(none.is_none());
    }

    #[test]
    fn test_pb_create_minimal() {
        let tempdir = TempDir::new().unwrap();
        let pb_dir = tempdir.path().join("pb");

        let partitions_path = tempdir.path().join("partitions.json");
        let partitions_file = File::create(&partitions_path).unwrap();
        serde_json::to_writer(&partitions_file, &PartitionsConfig::default()).unwrap();

        pb_create(CreateCommand {
            partitions: partitions_path,
            system_a: None,
            system_b: None,
            system_r: None,
            include_upload_manifest: false,
            out_dir: pb_dir.clone(),
        })
        .unwrap();

        let pb = ProductBundle::try_load_from(pb_dir).unwrap();
        assert_eq!(
            pb,
            ProductBundle::V2(ProductBundleV2 {
                partitions: PartitionsConfig::default(),
                system_a: None,
                system_b: None,
                system_r: None,
            })
        );
    }

    #[test]
    fn test_pb_create_a_and_r() {
        let tempdir = TempDir::new().unwrap();
        let pb_dir = tempdir.path().join("pb");

        let partitions_path = tempdir.path().join("partitions.json");
        let partitions_file = File::create(&partitions_path).unwrap();
        serde_json::to_writer(&partitions_file, &PartitionsConfig::default()).unwrap();

        let system_path = tempdir.path().join("system.json");
        let system_file = File::create(&system_path).unwrap();
        serde_json::to_writer(&system_file, &AssemblyManifest::default()).unwrap();

        pb_create(CreateCommand {
            partitions: partitions_path,
            system_a: Some(system_path.clone()),
            system_b: None,
            system_r: Some(system_path.clone()),
            include_upload_manifest: false,
            out_dir: pb_dir.clone(),
        })
        .unwrap();

        let pb = ProductBundle::try_load_from(pb_dir).unwrap();
        assert_eq!(
            pb,
            ProductBundle::V2(ProductBundleV2 {
                partitions: PartitionsConfig::default(),
                system_a: Some(AssemblyManifest::default()),
                system_b: None,
                system_r: Some(AssemblyManifest::default()),
            })
        );
    }

    #[test]
    fn test_write_upload_manifest() {
        let tempdir = TempDir::new().unwrap();

        let create_test_file = |name: &str| -> PathBuf {
            let path = tempdir.path().join(name);
            let mut file = File::create(&path).unwrap();
            write!(&mut file, "contents").unwrap();
            path
        };
        let product_bundle_path = create_test_file("product_bundle.json");
        let bootstrap_path = create_test_file("bootstrap");
        let bootloader_path = create_test_file("bootloader");
        let unlock_credential_path = create_test_file("unlock_credential");
        let zbi_path = create_test_file("zbi");

        let pb = ProductBundleV2 {
            partitions: PartitionsConfig {
                bootstrap_partitions: vec![BootstrapPartition {
                    name: "bootstrap".into(),
                    image: bootstrap_path.clone(),
                    condition: None,
                }],
                bootloader_partitions: vec![BootloaderPartition {
                    name: None,
                    image: bootloader_path.clone(),
                    partition_type: "type".into(),
                }],
                partitions: vec![],
                hardware_revision: "".into(),
                unlock_credentials: vec![unlock_credential_path.clone()],
            },
            system_a: Some(AssemblyManifest {
                images: vec![Image::ZBI { signed: false, path: zbi_path.clone() }],
            }),
            system_b: None,
            system_r: None,
        };

        let mut buf = Vec::new();
        write_upload_manifest(tempdir, &pb, &mut buf).unwrap();
        let json: serde_json::Value = serde_json::de::from_slice(&buf).unwrap();
        assert_eq!(
            serde_json::json!({
                "version": "1",
                "entries": [
                    {
                        "source": product_bundle_path.path_to_string().unwrap(),
                        "destination": "product_bundle.json",
                    },
                    {
                        "source": bootstrap_path.path_to_string().unwrap(),
                        "destination": "bootstrap",
                    },
                    {
                        "source": bootloader_path.path_to_string().unwrap(),
                        "destination": "bootloader",
                    },
                    {
                        "source": unlock_credential_path.path_to_string().unwrap(),
                        "destination": "unlock_credential",
                    },
                    {
                        "source": zbi_path.path_to_string().unwrap(),
                        "destination": "zbi",
                    },
                ]
            }),
            json
        );
    }
}
