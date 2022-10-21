// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Generate a transfer manifest for uploading and downloading a product bundle.

use anyhow::{bail, Context, Result};
use argh::FromArgs;
use assembly_manifest::AssemblyManifest;
use pathdiff::diff_paths;
use sdk_metadata::ProductBundle;
use std::fs::File;
use std::path::PathBuf;
use transfer_manifest::{
    ArtifactEntry, ArtifactType, TransferEntry, TransferManifest, TransferManifestV1,
};

/// Generate a transfer manifest for uploading and downloading a product bundle.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "generate-transfer-manifest")]
pub struct GenerateTransferManifest {
    /// path to a product bundle.
    #[argh(option)]
    product_bundle: PathBuf,

    /// path to the output transfer manifest.
    #[argh(option)]
    output: PathBuf,
}

impl GenerateTransferManifest {
    /// Generate the transfer manifest into `output`.
    pub async fn generate(self) -> Result<()> {
        let product_bundle = ProductBundle::try_load_from(&self.product_bundle)?;
        let product_bundle = match product_bundle {
            ProductBundle::V1(_) => bail!("Only v2 product bundles are supported"),
            ProductBundle::V2(pb) => pb,
        };

        let output_directory = &self
            .output
            .parent()
            .ok_or(anyhow::anyhow!("getting the parent of the output file"))?;
        let rebased_product_bundle_path = diff_paths(&self.product_bundle, output_directory)
            .context("rebasing product bundle directory")?;
        let canonical_product_bundle_path =
            &self.product_bundle.canonicalize().context("canonicalizing product bundle path")?;

        let mut entries = vec![];
        let mut product_bundle_entries = vec![];
        product_bundle_entries.push(ArtifactEntry { name: "product_bundle.json".into() });
        for partition in &product_bundle.partitions.bootstrap_partitions {
            product_bundle_entries.push(ArtifactEntry {
                name: diff_paths(&partition.image, &canonical_product_bundle_path)
                    .context("rebasing bootstrap partition")?,
            });
        }
        for partition in &product_bundle.partitions.bootloader_partitions {
            product_bundle_entries.push(ArtifactEntry {
                name: diff_paths(&partition.image, &canonical_product_bundle_path)
                    .context("rebasing bootloader partition")?,
            });
        }
        for credential in &product_bundle.partitions.unlock_credentials {
            product_bundle_entries.push(ArtifactEntry {
                name: diff_paths(&credential, &canonical_product_bundle_path)
                    .context("rebasing unlock credential")?,
            });
        }
        // Add the images from the systems.
        let mut system = |system: &Option<AssemblyManifest>| -> Result<()> {
            if let Some(system) = system {
                for image in &system.images {
                    product_bundle_entries.push(ArtifactEntry {
                        name: diff_paths(image.source(), &canonical_product_bundle_path)
                            .context("rebasing system image")?,
                    });
                }
            }
            Ok(())
        };
        system(&product_bundle.system_a)?;
        system(&product_bundle.system_b)?;
        system(&product_bundle.system_r)?;
        entries.push(TransferEntry {
            artifact_type: ArtifactType::ProductBundle,
            local: rebased_product_bundle_path.clone(),
            remote: "product_bundle".into(),
            entries: product_bundle_entries,
        });

        // Add all the blobs to the transfer manifest.
        if let Some(repository) = &product_bundle.repository {
            let blobs =
                product_bundle.blobs().await.context("gathering blobs from product bundle")?;
            let blob_entries =
                blobs.into_iter().map(|p| ArtifactEntry { name: p.into() }).collect();
            let local = diff_paths(&repository.blobs_path, canonical_product_bundle_path)
                .context("rebasing blobs path")?;
            let blob_transfer = TransferEntry {
                artifact_type: ArtifactType::Blobs,
                local,
                remote: "".into(),
                entries: blob_entries,
            };
            entries.push(blob_transfer);
        }

        let transfer_manifest = TransferManifest::V1(TransferManifestV1 { entries });
        let file = File::create(&self.output).context("creating transfer manifest")?;
        serde_json::to_writer(file, &transfer_manifest).context("writing transfer manifest")?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assembly_manifest::Image;
    use assembly_partitions_config::PartitionsConfig;
    use sdk_metadata::ProductBundleV2;
    use std::io::Write;
    use tempfile::tempdir;

    #[fuchsia::test]
    async fn test_generate() {
        let tempdir = tempdir().unwrap();
        let pb_path = tempdir.path().join("product_bundle");
        std::fs::create_dir_all(&pb_path).unwrap();

        let create_temp_file = |name: &str| -> PathBuf {
            let path = pb_path.join(name);
            let mut file = File::create(&path).unwrap();
            write!(file, "{}", name).unwrap();
            path
        };

        let pb = ProductBundle::V2(ProductBundleV2 {
            partitions: PartitionsConfig::default(),
            system_a: Some(AssemblyManifest {
                images: vec![
                    Image::ZBI { path: create_temp_file("zbi"), signed: false },
                    Image::FVM(create_temp_file("fvm")),
                    Image::QemuKernel(create_temp_file("kernel")),
                ],
            }),
            system_b: None,
            system_r: None,
            repository: None,
        });
        pb.write(&pb_path).unwrap();

        let output = tempdir.path().join("transfer.json");
        let cmd =
            GenerateTransferManifest { product_bundle: pb_path.clone(), output: output.clone() };
        cmd.generate().await.unwrap();

        let transfer_manifest_file = File::open(&output).unwrap();
        let transfer_manifest: TransferManifest =
            serde_json::from_reader(transfer_manifest_file).unwrap();
        assert_eq!(
            transfer_manifest,
            TransferManifest::V1(TransferManifestV1 {
                entries: vec![TransferEntry {
                    artifact_type: ArtifactType::ProductBundle,
                    local: "product_bundle".into(),
                    remote: "product_bundle".into(),
                    entries: vec![
                        ArtifactEntry { name: "product_bundle.json".into() },
                        ArtifactEntry { name: "zbi".into() },
                        ArtifactEntry { name: "fvm".into() },
                        ArtifactEntry { name: "kernel".into() },
                    ],
                },],
            })
        );
    }
}
