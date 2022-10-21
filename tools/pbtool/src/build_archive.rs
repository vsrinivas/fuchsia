// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Generate a build archive from a product bundle.

use anyhow::{bail, Context, Result};
use argh::FromArgs;
use assembly_manifest::{AssemblyManifest, Image};
use sdk_metadata::ProductBundle;
use std::fs::File;
use std::path::PathBuf;

/// Generate a build archive using the specified `args`.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "generate-build-archive")]
pub struct GenerateBuildArchive {
    /// path to a product bundle.
    #[argh(option)]
    product_bundle: PathBuf,

    /// path to the directory to write a build archive into.
    #[argh(option)]
    out_dir: PathBuf,
}

impl GenerateBuildArchive {
    pub fn generate(self) -> Result<()> {
        let product_bundle = ProductBundle::try_load_from(&self.product_bundle)?;
        let product_bundle = match product_bundle {
            ProductBundle::V1(_) => bail!("Only v2 product bundles are supported"),
            ProductBundle::V2(pb) => pb,
        };

        // Find the first assembly in the preferential order of A, B, then R.
        let assembly = if let Some(a) = product_bundle.system_a {
            a
        } else if let Some(b) = product_bundle.system_b {
            b
        } else if let Some(r) = product_bundle.system_r {
            r
        } else {
            bail!("The product bundle does not have any assembly systems");
        };

        // Ensure the `out_dir` exists.
        std::fs::create_dir_all(&self.out_dir)
            .with_context(|| format!("Creating the out_dir: {}", &self.out_dir.display()))?;

        // Collect the Images with the final destinations to add to an images manifest later.
        let mut images = vec![];

        // Pull out the relevant files.
        for image in assembly.images {
            let entry = match &image {
                Image::ZBI { path, signed: _ } => Some((path, "zircon-a.zbi")),
                Image::FVM(path) => Some((path, "storage-full.blk")),
                Image::QemuKernel(path) => Some((path, "qemu-kernel.kernel")),
                _ => None,
            };
            if let Some((path, name)) = entry {
                // Copy the image to the out_dir.
                let destination = self.out_dir.join(name);
                std::fs::copy(&path, &destination).with_context(|| {
                    format!("Copying image {} to {}", path.display(), destination.display())
                })?;

                // Create a new Image with the new path.
                let mut new_image = image.clone();
                new_image.set_source(name);
                images.push(new_image);
            }
        }

        // Write the images manifest with the rebased image paths.
        let images_manifest = AssemblyManifest { images };
        let images_manifest_file =
            File::create(self.out_dir.join("images.json")).context("Creating images manifest")?;
        serde_json::to_writer(images_manifest_file, &images_manifest)
            .context("Writing images manifest")?;

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

    #[test]
    fn test_generate_build_archive() {
        let tempdir = tempdir().unwrap();
        let create_temp_file = |name: &str| {
            let path = tempdir.path().join(name);
            let mut file = File::create(path).unwrap();
            write!(file, "{}", name).unwrap();
        };

        create_temp_file("zbi");
        create_temp_file("fvm");
        create_temp_file("kernel");

        let pb = ProductBundle::V2(ProductBundleV2 {
            partitions: PartitionsConfig::default(),
            system_a: Some(AssemblyManifest {
                images: vec![
                    Image::ZBI { path: tempdir.path().join("zbi"), signed: false },
                    Image::FVM(tempdir.path().join("fvm")),
                    Image::QemuKernel(tempdir.path().join("kernel")),
                ],
            }),
            system_b: None,
            system_r: None,
            repository: None,
        });
        let pb_path = tempdir.path().join("product_bundle");
        std::fs::create_dir_all(&pb_path).unwrap();
        pb.write(&pb_path).unwrap();

        let ba_path = tempdir.path().join("build_archive");
        let cmd =
            GenerateBuildArchive { product_bundle: pb_path.clone(), out_dir: ba_path.clone() };
        cmd.generate().unwrap();

        assert!(ba_path.join("zircon-a.zbi").exists());
        assert!(ba_path.join("storage-full.blk").exists());
        assert!(ba_path.join("qemu-kernel.kernel").exists());

        let images_manifest_file = File::open(ba_path.join("images.json")).unwrap();
        let images_manifest: AssemblyManifest =
            serde_json::from_reader(images_manifest_file).unwrap();
        assert_eq!(
            images_manifest,
            AssemblyManifest {
                images: vec![
                    Image::ZBI { path: "zircon-a.zbi".into(), signed: false },
                    Image::FVM("storage-full.blk".into()),
                    Image::QemuKernel("qemu-kernel.kernel".into()),
                ],
            }
        );
    }
}
