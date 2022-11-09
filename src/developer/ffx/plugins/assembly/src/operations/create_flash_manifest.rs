// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use assembly_manifest::AssemblyManifest;
use assembly_partitions_config::PartitionsConfig;
use ffx_assembly_args::CreateFlashManifestArgs;
use ffx_fastboot::manifest::FlashManifestVersion;
use sdk_metadata::{ProductBundle, ProductBundleV2};
use std::fs::File;
use std::io::BufReader;
use std::path::Path;

/// Create a flash manifest that can be used to flash the partitions of a target device.
pub fn create_flash_manifest(args: CreateFlashManifestArgs) -> Result<()> {
    let mut file = File::open(&args.partitions)
        .context(format!("Failed to open: {}", args.partitions.display()))?;
    let partitions_config = PartitionsConfig::from_reader(&mut file)
        .context("Failed to parse the partitions config")?;

    // Create a product bundle from the arguments.
    let product_bundle = ProductBundle::V2(ProductBundleV2 {
        partitions: partitions_config,
        system_a: args.system_a.as_ref().map(manifest_from_file).transpose()?,
        system_b: args.system_b.as_ref().map(manifest_from_file).transpose()?,
        system_r: args.system_r.as_ref().map(manifest_from_file).transpose()?,
        repositories: vec![],
        update_package_hash: None,
    });

    // Create a flash manifest from the product bundle.
    let manifest = FlashManifestVersion::from_product_bundle(&product_bundle)?;

    // Write the flash manifest.
    let flash_manifest_path = args.outdir.join("flash.json");
    let mut flash_manifest_file = File::create(&flash_manifest_path)
        .context(format!("Failed to create: {}", flash_manifest_path.display()))?;
    manifest.write(&mut flash_manifest_file)?;

    Ok(())
}

/// Read an AssemblyManifest from a file.
fn manifest_from_file(path: impl AsRef<Path>) -> Result<AssemblyManifest> {
    let file = File::open(path.as_ref())
        .context(format!("Failed to open the system images file: {}", path.as_ref().display()))?;
    serde_json::from_reader(BufReader::new(file))
        .context(format!("Failed to parse the system images file: {}", path.as_ref().display()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::{read_config, write_json_file};
    use serde_json::json;
    use std::fs;
    use tempfile::TempDir;

    struct TestFs {
        root: TempDir,
    }

    impl TestFs {
        fn new() -> TestFs {
            TestFs { root: TempDir::new().unwrap() }
        }

        fn write(&self, rel_path: &str, value: serde_json::Value) {
            let path = self.root.path().join(rel_path);
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            write_json_file(&path, &value).unwrap()
        }

        fn assert_eq(&self, rel_path: &str, expected: serde_json::Value) {
            let path = self.root.path().join(rel_path);
            let actual: serde_json::Value = read_config(&path).unwrap();
            assert_eq!(actual, expected);
        }

        fn path(&self, rel_path: &str) -> std::path::PathBuf {
            self.root.path().join(rel_path)
        }
    }

    #[test]
    fn test_create_flash_manifest() {
        let test_fs = TestFs::new();
        test_fs.write(
            "partitions.json",
            json!({
                "hardware_revision": "board_name",
                "unlock_credentials": [],
                "bootloader_partitions": [
                    {
                        "image": "path/to/bootloader",
                        "name": "boot1",
                        "type": "boot",
                    }
                ],
                "partitions": [
                    {
                        "name": "part1",
                        "slot": "A",
                        "type": "ZBI",
                    },
                    {
                        "name": "part2",
                        "slot": "B",
                        "type": "ZBI",
                    },
                    {
                        "name": "part3",
                        "slot": "R",
                        "type": "ZBI",
                    },
                    {
                        "name": "part4",
                        "slot": "A",
                        "type": "VBMeta",
                    },
                    {
                        "name": "part5",
                        "slot": "B",
                        "type": "VBMeta",
                    },
                    {
                        "name": "part6",
                        "slot": "R",
                        "type": "VBMeta",
                    },
                    {
                        "name": "part7",
                        "type": "FVM",
                    },
                ],
            }),
        );
        test_fs.write(
            "system_a.json",
            json!([
                {
                    "type": "blk",
                    "name": "fvm.fastboot",
                    "path": "fvm-a.fastboot.blk"
                },
                {
                    "type": "vbmeta",
                    "name": "zircon-a",
                    "path": "fuchsia-a.vbmeta"
                },
                {
                    "type": "zbi",
                    "name": "zircon-a",
                    "path": "fuchsia-a.zbi",
                    "signed": false,
                },
            ]),
        );
        test_fs.write(
            "system_b.json",
            json!([
                {
                    "type": "blk",
                    "name": "fvm.fastboot",
                    "path": "fvm-b.fastboot.blk"
                },
                {
                    "type": "vbmeta",
                    "name": "zircon-a",
                    "path": "fuchsia-b.vbmeta"
                },
                {
                    "type": "zbi",
                    "name": "zircon-a",
                    "path": "fuchsia-b.zbi",
                    "signed": false,
                },
            ]),
        );
        test_fs.write(
            "system_r.json",
            json!([
                {
                    "type": "vbmeta",
                    "name": "zircon-a",
                    "path": "recovery.vbmeta"
                },
                {
                    "type": "zbi",
                    "name": "zircon-a",
                    "path": "recovery.zbi",
                    "signed": false,
                },
            ]),
        );

        create_flash_manifest(CreateFlashManifestArgs {
            partitions: test_fs.path("partitions.json"),
            system_a: Some(test_fs.path("system_a.json")),
            system_b: Some(test_fs.path("system_b.json")),
            system_r: Some(test_fs.path("system_r.json")),
            outdir: test_fs.root.path().to_path_buf(),
        })
        .unwrap();

        test_fs.assert_eq(
            "flash.json",
            json!({
                "manifest": {
                    "hw_revision": "board_name",
                    "products": [
                        {
                            "name": "recovery",
                            "requires_unlock": false,
                            "bootloader_partitions": [
                                {
                                    "name": "boot1",
                                    "path": "path/to/bootloader",
                                },
                            ],
                            "partitions": [
                                {
                                    "name": "part1",
                                    "path": "recovery.zbi",
                                },
                                {
                                    "name": "part2",
                                    "path": "recovery.zbi",
                                },
                                {
                                    "name": "part3",
                                    "path": "recovery.zbi",
                                },
                                {
                                    "name": "part4",
                                    "path": "recovery.vbmeta",
                                },
                                {
                                    "name": "part5",
                                    "path": "recovery.vbmeta",
                                },
                                {
                                    "name": "part6",
                                    "path": "recovery.vbmeta",
                                },
                            ],
                        },
                        {
                            "name": "fuchsia_only",
                            "requires_unlock": false,
                            "bootloader_partitions": [
                                {
                                    "name": "boot1",
                                    "path": "path/to/bootloader",
                                },
                            ],
                            "partitions": [
                                {
                                    "name": "part1",
                                    "path": "fuchsia-a.zbi",
                                },
                                {
                                    "name": "part2",
                                    "path": "fuchsia-b.zbi",
                                },
                                {
                                    "name": "part3",
                                    "path": "recovery.zbi",
                                },
                                {
                                    "name": "part4",
                                    "path": "fuchsia-a.vbmeta",
                                },
                                {
                                    "name": "part5",
                                    "path": "fuchsia-b.vbmeta",
                                },
                                {
                                    "name": "part6",
                                    "path": "recovery.vbmeta",
                                },
                                {
                                    "name": "part7",
                                    "path": "fvm-a.fastboot.blk",
                                },
                            ],
                        },
                        {
                            "name": "fuchsia",
                            "requires_unlock": false,
                            "bootloader_partitions": [
                                {
                                    "name": "boot1",
                                    "path": "path/to/bootloader",
                                },
                            ],
                            "partitions": [
                                {
                                    "name": "part1",
                                    "path": "fuchsia-a.zbi",
                                },
                                {
                                    "name": "part2",
                                    "path": "fuchsia-b.zbi",
                                },
                                {
                                    "name": "part3",
                                    "path": "recovery.zbi",
                                },
                                {
                                    "name": "part4",
                                    "path": "fuchsia-a.vbmeta",
                                },
                                {
                                    "name": "part5",
                                    "path": "fuchsia-b.vbmeta",
                                },
                                {
                                    "name": "part6",
                                    "path": "recovery.vbmeta",
                                },
                                {
                                    "name": "part7",
                                    "path": "fvm-a.fastboot.blk",
                                },
                            ],
                        },
                    ],
                },
                "version": 3,
            }),
        );
    }
}
