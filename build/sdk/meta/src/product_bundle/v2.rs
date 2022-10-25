// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Version 2 of the Product Bundle format.
//!
//! This format is drastically different from Version 1 in that all the contents are expected to
//! stay as implementation detail of ffx. The outputs of assembly are fed directly into the fields
//! of the Product Bundle, and the flash and emulator manifests are not constructed until the
//! Product Bundle is read by `ffx emu start` and `ffx target flash`. This makes the format
//! simpler, and more aligned with how images are assembled.
//!
//! Note on paths
//! -------------
//! PBv2 is a directory containing images and other artifacts necessary to flash, emulator, and
//! update a product. When a Product Bundle is written to disk, the paths inside _must_ all be
//! relative to the Product Bundle itself, to ensure that the directory remains portable (can be
//! moved, zipped, tarred, downloaded on another machine).

use anyhow::{anyhow, Context, Result};
use assembly_manifest::AssemblyManifest;
use assembly_partitions_config::PartitionsConfig;
use camino::Utf8PathBuf;
use fidl_fuchsia_developer_ffx::ListFields;
use fuchsia_repo::{repo_client::RepoClient, repository::FileSystemRepository};
use pathdiff::diff_paths;
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::path::Path;

/// Description of the data needed to set up (flash) a device.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ProductBundleV2 {
    /// The physical partitions of the target to place images into.
    pub partitions: PartitionsConfig,

    /// An assembled system that should be placed in slot A on the target.
    #[serde(default)]
    pub system_a: Option<AssemblyManifest>,

    /// An assembled system that should be placed in slot B on the target.
    #[serde(default)]
    pub system_b: Option<AssemblyManifest>,

    /// An assembled system that should be placed in slot R on the target.
    #[serde(default)]
    pub system_r: Option<AssemblyManifest>,

    /// The repositories that hold the TUF metadata, packages, and blobs.
    #[serde(default)]
    pub repositories: Vec<Repository>,
}

/// A repository that holds all the packages, blobs, and keys.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Repository {
    /// The unique name of the repository, such as "fuchsia.com".
    pub name: String,

    /// The path to the TUF repository, relative to the product bundle directory.
    pub metadata_path: Utf8PathBuf,

    /// The path to the blobs directory, relative to the product bundle directory.
    pub blobs_path: Utf8PathBuf,
}

impl Repository {
    /// Return the paths-on-host of the blobs contained in the product bundle.
    pub async fn blobs(&self) -> Result<HashSet<Utf8PathBuf>> {
        let mut all_blobs = HashSet::<Utf8PathBuf>::new();
        let repo = FileSystemRepository::new(self.metadata_path.clone(), self.blobs_path.clone());
        let mut client =
            RepoClient::from_trusted_remote(&repo).await.context("creating the repo client")?;
        client.update().await.context("updating the repo metadata")?;
        let packages =
            client.list_packages(ListFields::empty()).await.context("listing packages")?;
        for package in &packages {
            if let Some(name) = &package.name {
                if let Some(blobs) =
                    client.show_package(name.clone()).await.context("showing package")?
                {
                    all_blobs.extend(blobs.iter().filter_map(|e| e.hash.clone()).map(|p| p.into()));
                }
            }
        }
        Ok(all_blobs)
    }
}

impl ProductBundleV2 {
    /// Convert all the paths from relative to absolute, assuming `product_bundle_dir` is the
    /// current base all the paths are relative to.
    ///
    /// Note: This function is intentionally only accessible inside the crate to ensure this method
    /// is only called during deserialization. Clients should not be canonicalizing their own
    /// paths.
    pub(crate) fn canonicalize_paths(
        &mut self,
        product_bundle_dir: impl AsRef<Path>,
    ) -> Result<()> {
        let product_bundle_dir = product_bundle_dir.as_ref();

        // Canonicalize the partitions.
        for part in &mut self.partitions.bootstrap_partitions {
            part.image = product_bundle_dir.join(&part.image).canonicalize()?;
        }
        for part in &mut self.partitions.bootloader_partitions {
            part.image = product_bundle_dir.join(&part.image).canonicalize()?;
        }
        for cred in &mut self.partitions.unlock_credentials {
            *cred = product_bundle_dir.join(&cred).canonicalize()?;
        }

        // Canonicalize the systems.
        let canonicalize_system = |system: &mut Option<AssemblyManifest>| -> Result<()> {
            if let Some(system) = system {
                for image in &mut system.images {
                    image.set_source(product_bundle_dir.join(image.source()).canonicalize()?);
                }
            }
            Ok(())
        };
        canonicalize_system(&mut self.system_a)?;
        canonicalize_system(&mut self.system_b)?;
        canonicalize_system(&mut self.system_r)?;

        for repository in &mut self.repositories {
            let canonicalize_dir = |path: &Utf8PathBuf| -> Result<Utf8PathBuf> {
                let dir = product_bundle_dir.join(path);
                // Create the directory to ensure that canonicalize will work.
                std::fs::create_dir_all(&dir)
                    .with_context(|| format!("Creating the directory: {}", dir.display()))?;
                let path = Utf8PathBuf::from_path_buf(dir.canonicalize()?)
                    .map_err(|_| anyhow::anyhow!("converting to utf8 path: {}", &path))?;
                Ok(path)
            };
            repository.metadata_path = canonicalize_dir(&repository.metadata_path)?;
            repository.blobs_path = canonicalize_dir(&repository.blobs_path)?;
        }

        Ok(())
    }

    /// Convert all the paths from absolute to relative, assuming `product_bundle_dir` is the
    /// new base all the paths should be relative to.
    ///
    /// Note: This function is intentionally only accessible inside the crate to ensure this method
    /// is only called during deserialization. Clients should not be canonicalizing their own
    /// paths.
    pub(crate) fn relativize_paths(&mut self, product_bundle_dir: impl AsRef<Path>) -> Result<()> {
        let product_bundle_dir = product_bundle_dir.as_ref();

        // Relativize the partitions.
        for part in &mut self.partitions.bootstrap_partitions {
            part.image =
                diff_paths(&part.image, &product_bundle_dir).context("rebasing file path")?;
        }
        for part in &mut self.partitions.bootloader_partitions {
            part.image =
                diff_paths(&part.image, &product_bundle_dir).context("rebasing file path")?;
        }
        for cred in &mut self.partitions.unlock_credentials {
            *cred = diff_paths(&cred, &product_bundle_dir).context("rebasing file path")?;
        }

        // Relativize the systems.
        let relativize_system = |system: &mut Option<AssemblyManifest>| -> Result<()> {
            if let Some(system) = system {
                for image in &mut system.images {
                    let path = diff_paths(&image.source(), &product_bundle_dir)
                        .ok_or(anyhow!("failed to rebase the file"))?;
                    image.set_source(path);
                }
            }
            Ok(())
        };
        relativize_system(&mut self.system_a)?;
        relativize_system(&mut self.system_b)?;
        relativize_system(&mut self.system_r)?;

        for repository in &mut self.repositories {
            let relativize_dir = |path: &Utf8PathBuf| -> Result<Utf8PathBuf> {
                let dir = diff_paths(&path, &product_bundle_dir).context("rebasing repository")?;
                let path = Utf8PathBuf::from_path_buf(dir)
                    .map_err(|_| anyhow::anyhow!("converting to utf8 path: {}", &path))?;
                Ok(path)
            };
            repository.metadata_path = relativize_dir(&repository.metadata_path)?;
            repository.blobs_path = relativize_dir(&repository.blobs_path)?;
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assembly_manifest::Image;
    use assembly_partitions_config::{BootloaderPartition, BootstrapPartition, Partition, Slot};
    use camino::Utf8Path;
    use fuchsia_repo::test_utils;
    use std::fs::File;
    use std::io::Write;
    use std::path::PathBuf;
    use tempfile::TempDir;

    #[test]
    fn test_canonicalize_no_paths() {
        let mut pb = ProductBundleV2 {
            partitions: PartitionsConfig {
                bootstrap_partitions: vec![],
                bootloader_partitions: vec![],
                partitions: vec![],
                hardware_revision: "board".into(),
                unlock_credentials: vec![],
            },
            system_a: None,
            system_b: None,
            system_r: None,
            repositories: vec![],
        };
        let result = pb.canonicalize_paths(&PathBuf::from("path/to/product_bundle"));
        assert!(result.is_ok());
    }

    #[test]
    fn test_canonicalize_with_paths() {
        let tempdir = TempDir::new().unwrap();
        let create_temp_file = |name: &str| {
            let path = tempdir.path().join(name);
            let mut file = File::create(path).unwrap();
            write!(file, "{}", name).unwrap();
        };

        // These files must exist for canonicalize() to work.
        create_temp_file("bootstrap");
        create_temp_file("bootloader");
        create_temp_file("zbi");
        create_temp_file("vbmeta");
        create_temp_file("fvm");
        create_temp_file("unlock_credentials");

        let mut pb = ProductBundleV2 {
            partitions: PartitionsConfig {
                bootstrap_partitions: vec![BootstrapPartition {
                    name: "bootstrap".into(),
                    image: "bootstrap".into(),
                    condition: None,
                }],
                bootloader_partitions: vec![BootloaderPartition {
                    partition_type: "bl2".into(),
                    name: None,
                    image: "bootloader".into(),
                }],
                partitions: vec![
                    Partition::ZBI { name: "zbi".into(), slot: Slot::A },
                    Partition::VBMeta { name: "vbmeta".into(), slot: Slot::A },
                    Partition::FVM { name: "fvm".into() },
                ],
                hardware_revision: "board".into(),
                unlock_credentials: vec!["unlock_credentials".into()],
            },
            system_a: Some(AssemblyManifest {
                images: vec![
                    Image::ZBI { path: "zbi".into(), signed: false },
                    Image::VBMeta("vbmeta".into()),
                    Image::FVM("fvm".into()),
                ],
            }),
            system_b: None,
            system_r: None,
            repositories: vec![],
        };
        let result = pb.canonicalize_paths(tempdir.path());
        assert!(result.is_ok());
    }

    #[test]
    fn test_relativize_no_paths() {
        let mut pb = ProductBundleV2 {
            partitions: PartitionsConfig {
                bootstrap_partitions: vec![],
                bootloader_partitions: vec![],
                partitions: vec![],
                hardware_revision: "board".into(),
                unlock_credentials: vec![],
            },
            system_a: None,
            system_b: None,
            system_r: None,
            repositories: vec![],
        };
        let result = pb.relativize_paths(&PathBuf::from("path/to/product_bundle"));
        assert!(result.is_ok());
    }

    #[test]
    fn test_relativize_with_paths() {
        let tempdir = TempDir::new().unwrap();
        let create_temp_file = |name: &str| {
            let path = tempdir.path().join(name);
            let mut file = File::create(path).unwrap();
            write!(file, "{}", name).unwrap();
        };

        // These files must exist for diff_paths() to work.
        create_temp_file("bootstrap");
        create_temp_file("bootloader");
        create_temp_file("zbi");
        create_temp_file("vbmeta");
        create_temp_file("fvm");
        create_temp_file("unlock_credentials");

        let mut pb = ProductBundleV2 {
            partitions: PartitionsConfig {
                bootstrap_partitions: vec![BootstrapPartition {
                    name: "bootstrap".into(),
                    image: tempdir.path().join("bootstrap"),
                    condition: None,
                }],
                bootloader_partitions: vec![BootloaderPartition {
                    partition_type: "bl2".into(),
                    name: None,
                    image: tempdir.path().join("bootloader"),
                }],
                partitions: vec![
                    Partition::ZBI { name: "zbi".into(), slot: Slot::A },
                    Partition::VBMeta { name: "vbmeta".into(), slot: Slot::A },
                    Partition::FVM { name: "fvm".into() },
                ],
                hardware_revision: "board".into(),
                unlock_credentials: vec![tempdir.path().join("unlock_credentials")],
            },
            system_a: Some(AssemblyManifest {
                images: vec![
                    Image::ZBI { path: tempdir.path().join("zbi"), signed: false },
                    Image::VBMeta(tempdir.path().join("vbmeta")),
                    Image::FVM(tempdir.path().join("fvm")),
                ],
            }),
            system_b: None,
            system_r: None,
            repositories: vec![],
        };
        let result = pb.relativize_paths(tempdir.path());
        assert!(result.is_ok());
    }

    #[fuchsia::test]
    async fn test_blobs() {
        let tempdir = TempDir::new().unwrap();
        let dir = Utf8Path::from_path(&tempdir.path()).unwrap();
        let _repo = test_utils::make_pm_repo_dir(&tempdir.path()).await;

        let pb = ProductBundleV2 {
            partitions: PartitionsConfig::default(),
            system_a: None,
            system_b: None,
            system_r: None,
            repositories: vec![Repository {
                name: "fuchsia.com".into(),
                metadata_path: dir.join("repository"),
                blobs_path: dir.join("repository").join("blobs"),
            }],
        };

        let expected = HashSet::from([
            "050907f009ff634f9aa57bff541fb9e9c2c62b587c23578e77637cda3bd69458".into(),
            "2881455493b5870aaea36537d70a2adc635f516ac2092598f4b6056dabc6b25d".into(),
            "548981eb310ddc4098fb5c63692e19ac4ae287b13d0e911fbd9f7819ac22491c".into(),
            "72e1e7a504f32edf4f23e7e8a3542c1d77d12541142261cfe272decfa75f542d".into(),
            "8a8a5f07f935a4e8e1fd1a1eda39da09bb2438ec0adfb149679ddd6e7e1fbb4f".into(),
            "ecc11f7f4b763c5a21be2b4159c9818bbe22ca7e6d8100a72f6a41d3d7b827a9".into(),
        ]);

        let blobs = pb.repositories[0].blobs().await.unwrap();
        assert_eq!(expected, blobs);
    }
}
