// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! FFX plugin for constructing product bundles, which are distributable containers for a product's
//! images and packages, and can be used to emulate, flash, or update a product.

use anyhow::{Context, Result};
use assembly_manifest::AssemblyManifest;
use assembly_partitions_config::PartitionsConfig;
use ffx_core::ffx_plugin;
use ffx_product_create_args::CreateCommand;
use sdk_metadata::{ProductBundle, ProductBundleV2, VersionedProductBundle};
use std::fs::File;
use std::path::PathBuf;

/// Create a product bundle.
#[ffx_plugin("product.experimental")]
fn pb_create(cmd: CreateCommand) -> Result<()> {
    // Make sure `out_dir` is created.
    std::fs::create_dir_all(&cmd.out_dir).context("Creating the out_dir")?;

    let partitions_file = File::open(cmd.partitions).context("Opening partitions config")?;
    let partitions: PartitionsConfig =
        serde_json::from_reader(partitions_file).context("Parsing partitions config")?;

    let product_bundle = ProductBundle::V2(VersionedProductBundle::V2(ProductBundleV2 {
        name: "product.board".into(),
        partitions,
        system_a: load_assembly_manifest(&cmd.system_a)?,
        system_b: load_assembly_manifest(&cmd.system_b)?,
        system_r: load_assembly_manifest(&cmd.system_r)?,
    }));
    let product_bundle_file = File::create(cmd.out_dir.join("product_bundle.json"))
        .context("creating product_bundle.json file")?;
    serde_json::to_writer(product_bundle_file, &product_bundle)
        .context("writing product_bundle.json file")?;
    Ok(())
}

/// Open and parse an AssemblyManifest from a path.
/// Returns None if the given path is None.
fn load_assembly_manifest(path: &Option<PathBuf>) -> Result<Option<AssemblyManifest>> {
    if let Some(path) = path {
        let file = File::open(path)
            .with_context(|| format!("Opening assembly manifest: {}", path.display()))?;
        let manifest: AssemblyManifest = serde_json::from_reader(file)
            .with_context(|| format!("Parsing assembly manifest: {}", path.display()))?;
        Ok(Some(manifest))
    } else {
        Ok(None)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use assembly_manifest::AssemblyManifest;
    use assembly_partitions_config::PartitionsConfig;
    use std::io::Write;
    use tempfile::{NamedTempFile, TempDir};

    #[test]
    fn test_load_assembly_manifest() {
        let manifest_file = NamedTempFile::new().unwrap();
        let manifest = AssemblyManifest::default();
        serde_json::to_writer(&manifest_file, &manifest).unwrap();

        let mut error_file = NamedTempFile::new().unwrap();
        error_file.write_all("error".as_bytes()).unwrap();

        let parsed = load_assembly_manifest(&Some(manifest_file.path().to_path_buf())).unwrap();
        assert!(parsed.is_some());

        let error = load_assembly_manifest(&Some(error_file.path().to_path_buf()));
        assert!(error.is_err());

        let none = load_assembly_manifest(&None).unwrap();
        assert!(none.is_none());
    }

    #[test]
    fn test_pb_create_minimal() {
        let tempdir = TempDir::new().unwrap();
        let pb_dir = tempdir.path().join("pb");
        let pb_manifest = pb_dir.join("product_bundle.json");

        let partitions_path = tempdir.path().join("partitions.json");
        let partitions_file = File::create(&partitions_path).unwrap();
        serde_json::to_writer(&partitions_file, &PartitionsConfig::default()).unwrap();

        pb_create(CreateCommand {
            partitions: partitions_path,
            system_a: None,
            system_b: None,
            system_r: None,
            out_dir: pb_dir.clone(),
        })
        .unwrap();

        let pb_file = File::open(pb_manifest).unwrap();
        let pb: ProductBundle = serde_json::from_reader(pb_file).unwrap();
        assert_eq!(
            pb,
            ProductBundle::V2(VersionedProductBundle::V2(ProductBundleV2 {
                name: "product.board".into(),
                partitions: PartitionsConfig::default(),
                system_a: None,
                system_b: None,
                system_r: None,
            }))
        );
    }

    #[test]
    fn test_pb_create_a_and_r() {
        let tempdir = TempDir::new().unwrap();
        let pb_dir = tempdir.path().join("pb");
        let pb_manifest = pb_dir.join("product_bundle.json");

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
            out_dir: pb_dir.clone(),
        })
        .unwrap();

        let pb_file = File::open(pb_manifest).unwrap();
        let pb: ProductBundle = serde_json::from_reader(pb_file).unwrap();
        assert_eq!(
            pb,
            ProductBundle::V2(VersionedProductBundle::V2(ProductBundleV2 {
                name: "product.board".into(),
                partitions: PartitionsConfig::default(),
                system_a: Some(AssemblyManifest::default()),
                system_b: None,
                system_r: Some(AssemblyManifest::default()),
            }))
        );
    }
}
