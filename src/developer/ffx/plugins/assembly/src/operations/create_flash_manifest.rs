// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use assembly_manifest::{AssemblyManifest, Image};
use assembly_partitions_config::{Partition, PartitionsConfig, Slot};
use assembly_util::PathToStringExt;
use ffx_assembly_args::CreateFlashManifestArgs;
use ffx_fastboot::manifest::{v3, FlashManifestVersion};
use std::collections::BTreeMap;
use std::fs::File;
use std::io::BufReader;
use std::path::Path;

/// The type of the image used in the below ImageMap.
#[derive(Debug, PartialOrd, Ord, PartialEq, Eq)]
enum ImageType {
    ZBI,
    VBMeta,
    FVM,
}

/// A map from a slot to the image paths assigned to that slot.
type ImageMap = BTreeMap<Slot, BTreeMap<ImageType, String>>;

/// Create a flash manifest that can be used to flash the partitions of a target device.
pub fn create_flash_manifest(args: CreateFlashManifestArgs) -> Result<()> {
    let mut file = File::open(&args.partitions)
        .context(format!("Failed to open: {}", args.partitions.display()))?;
    let partitions_config = PartitionsConfig::from_reader(&mut file)
        .context("Failed to parse the partitions config")?;

    // Copy the unlock credentials from the partitions config to the flash manifest.
    let mut credentials = vec![];
    for c in &partitions_config.unlock_credentials {
        credentials.push(c.path_to_string()?);
    }

    // Copy the bootloader partitions from the partitions config to the flash manifest.
    let mut bootloader_partitions = vec![];
    for p in &partitions_config.bootloader_partitions {
        if let Some(name) = &p.name {
            bootloader_partitions.push(v3::Partition {
                name: name.to_string(),
                path: p.image.path_to_string()?,
                condition: None,
            });
        }
    }

    // Copy the bootloader partitions from the partitions config to the flash manifest. Join them
    // with the bootloader partitions, because they are always flashed together.
    let mut all_bootloader_partitions = bootloader_partitions.clone();
    for p in &partitions_config.bootstrap_partitions {
        let condition = if let Some(c) = &p.condition {
            Some(v3::Condition { variable: c.variable.to_string(), value: c.value.to_string() })
        } else {
            None
        };
        all_bootloader_partitions.push(v3::Partition {
            name: p.name.to_string(),
            path: p.image.path_to_string()?,
            condition,
        });
    }

    // Create a map from slot to available images by name (zbi, vbmeta, fvm).
    let mut image_map: ImageMap = BTreeMap::new();
    if let Some(manifest) = args.system_a.as_ref().map(manifest_from_file).transpose()? {
        add_images_to_map(&mut image_map, &manifest, Slot::A)?;
    }
    if let Some(manifest) = args.system_b.as_ref().map(manifest_from_file).transpose()? {
        add_images_to_map(&mut image_map, &manifest, Slot::B)?;
    }
    if let Some(manifest) = args.system_r.as_ref().map(manifest_from_file).transpose()? {
        add_images_to_map(&mut image_map, &manifest, Slot::R)?;
    }

    // Define the flashable "products".
    let mut products = vec![];
    products.push(v3::Product {
        name: "recovery".into(),
        bootloader_partitions: bootloader_partitions.clone(),
        partitions: get_mapped_partitions(
            &partitions_config.partitions,
            &image_map,
            /*is_recovery=*/ true,
        ),
        oem_files: vec![],
        requires_unlock: false,
    });
    products.push(v3::Product {
        name: "fuchsia_only".into(),
        bootloader_partitions: bootloader_partitions.clone(),
        partitions: get_mapped_partitions(
            &partitions_config.partitions,
            &image_map,
            /*is_recovery=*/ false,
        ),
        oem_files: vec![],
        requires_unlock: false,
    });
    products.push(v3::Product {
        name: "fuchsia".into(),
        bootloader_partitions: all_bootloader_partitions.clone(),
        partitions: get_mapped_partitions(
            &partitions_config.partitions,
            &image_map,
            /*is_recovery=*/ false,
        ),
        oem_files: vec![],
        requires_unlock: !partitions_config.bootstrap_partitions.is_empty(),
    });
    if !partitions_config.bootstrap_partitions.is_empty() {
        products.push(v3::Product {
            name: "bootstrap".into(),
            bootloader_partitions: all_bootloader_partitions.clone(),
            partitions: vec![],
            oem_files: vec![],
            requires_unlock: true,
        });
    }

    // Write the flash manifest.
    let manifest = FlashManifestVersion::V3(v3::FlashManifest {
        hw_revision: partitions_config.hardware_revision,
        credentials,
        products,
    });
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

/// Add a set of images from |manifest| to the |image_map|, assigning them to |slot|. This ignores
/// all images other than the ZBI, VBMeta, and fastboot FVM.
fn add_images_to_map(
    image_map: &mut ImageMap,
    manifest: &AssemblyManifest,
    slot: Slot,
) -> Result<()> {
    let slot_entry = image_map.entry(slot).or_insert(BTreeMap::new());
    for image in &manifest.images {
        match image {
            Image::ZBI { path, .. } => slot_entry.insert(ImageType::ZBI, path.path_to_string()?),
            Image::VBMeta(path) => slot_entry.insert(ImageType::VBMeta, path.path_to_string()?),
            Image::FVMFastboot(path) => {
                if let Slot::R = slot {
                    // Recovery should not include a separate FVM, because it is embedded into the
                    // ZBI as a ramdisk.
                    None
                } else {
                    slot_entry.insert(ImageType::FVM, path.path_to_string()?)
                }
            }
            _ => None,
        };
    }
    Ok(())
}

/// Construct a list of partitions to add to the flash manifest by mapping the partitions to the
/// images. If |is_recovery|, then put the recovery images in every slot.
fn get_mapped_partitions(
    partitions: &Vec<Partition>,
    image_map: &ImageMap,
    is_recovery: bool,
) -> Vec<v3::Partition> {
    let mut mapped_partitions = vec![];

    // Assign the images to particular partitions. If |is_recovery|, then we use the recovery
    // images for all slots.
    for p in partitions {
        let (partition, name, slot) = match p {
            Partition::ZBI { name, slot } => (name, ImageType::ZBI, slot),
            Partition::VBMeta { name, slot } => (name, ImageType::VBMeta, slot),

            // Arbitrarily, take the fvm from the slot A system.
            Partition::FVM { name } => (name, ImageType::FVM, &Slot::A),
        };

        if let Some(slot) = match is_recovery {
            // If this is recovery mode, then fill every partition with images from the slot R
            // system.
            true => image_map.get(&Slot::R),
            false => image_map.get(slot),
        } {
            if let Some(path) = slot.get(&name) {
                mapped_partitions.push(v3::Partition {
                    name: partition.to_string(),
                    path: path.to_string(),
                    condition: None,
                });
            }
        }
    }

    mapped_partitions
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::{read_config, write_json_file};
    use assembly_manifest::{AssemblyManifest, Image};
    use maplit::btreemap;
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

    #[test]
    fn test_add_images_to_map() {
        let mut image_map: ImageMap = BTreeMap::new();
        let manifest = AssemblyManifest {
            images: vec![
                Image::ZBI { path: "path/to/fuchsia.zbi".into(), signed: false },
                Image::VBMeta("path/to/fuchsia.vbmeta".into()),
                Image::FVMFastboot("path/to/fvm.fastboot.blk".into()),
                // These should be ignored.
                Image::FVM("path/to/fvm.blk".into()),
                Image::BasePackage("path/to/base".into()),
            ],
        };
        add_images_to_map(&mut image_map, &manifest, Slot::A).unwrap();
        assert_eq!(image_map.len(), 1);
        assert_eq!(image_map[&Slot::A].len(), 3);
        assert_eq!(image_map[&Slot::A][&ImageType::ZBI], "path/to/fuchsia.zbi");
        assert_eq!(image_map[&Slot::A][&ImageType::VBMeta], "path/to/fuchsia.vbmeta");
        assert_eq!(image_map[&Slot::A][&ImageType::FVM], "path/to/fvm.fastboot.blk");

        add_images_to_map(&mut image_map, &manifest, Slot::B).unwrap();
        assert_eq!(image_map.len(), 2);
        assert_eq!(image_map[&Slot::B].len(), 3);
        assert_eq!(image_map[&Slot::B][&ImageType::ZBI], "path/to/fuchsia.zbi");
        assert_eq!(image_map[&Slot::B][&ImageType::VBMeta], "path/to/fuchsia.vbmeta");
        assert_eq!(image_map[&Slot::B][&ImageType::FVM], "path/to/fvm.fastboot.blk");

        add_images_to_map(&mut image_map, &manifest, Slot::R).unwrap();
        assert_eq!(image_map.len(), 3);
        assert_eq!(image_map[&Slot::R].len(), 2);
        assert_eq!(image_map[&Slot::R][&ImageType::ZBI], "path/to/fuchsia.zbi");
        assert_eq!(image_map[&Slot::R][&ImageType::VBMeta], "path/to/fuchsia.vbmeta");
    }

    #[test]
    fn test_get_mapped_partitions_no_slots() {
        let partitions = vec![];
        let image_map: ImageMap = btreemap! {
            Slot::A => btreemap!{
                ImageType::ZBI => "zbi".into(),
                ImageType::VBMeta => "vbmeta".into(),
                ImageType::FVM => "fvm".into(),
            },
        };
        let mapped = get_mapped_partitions(&partitions, &image_map, /*is_recovery=*/ false);
        assert!(mapped.is_empty());
    }

    #[test]
    fn test_get_mapped_partitions_slot_a_only() {
        let partitions = vec![
            Partition::ZBI { name: "part1".into(), slot: Slot::A },
            Partition::VBMeta { name: "part2".into(), slot: Slot::A },
            Partition::FVM { name: "part3".into() },
        ];
        let image_map: ImageMap = btreemap! {
            Slot::A => btreemap!{
                ImageType::ZBI => "zbi_a".into(),
                ImageType::VBMeta => "vbmeta_a".into(),
                ImageType::FVM => "fvm_a".into(),
            },
            Slot::B => btreemap!{
                ImageType::ZBI => "zbi_b".into(),
                ImageType::VBMeta => "vbmeta_b".into(),
                ImageType::FVM => "fvm_b".into(),
            },
            Slot::R => btreemap!{
                ImageType::ZBI => "zbi_r".into(),
                ImageType::VBMeta => "vbmeta_r".into(),
            },
        };
        let mapped = get_mapped_partitions(&partitions, &image_map, /*is_recovery=*/ false);
        assert_eq!(
            mapped,
            vec![
                v3::Partition { name: "part1".into(), path: "zbi_a".into(), condition: None },
                v3::Partition { name: "part2".into(), path: "vbmeta_a".into(), condition: None },
                v3::Partition { name: "part3".into(), path: "fvm_a".into(), condition: None },
            ]
        );
    }

    #[test]
    fn test_get_mapped_partitions_all_slots() {
        let partitions = vec![
            Partition::ZBI { name: "part1".into(), slot: Slot::A },
            Partition::VBMeta { name: "part2".into(), slot: Slot::A },
            Partition::ZBI { name: "part3".into(), slot: Slot::B },
            Partition::VBMeta { name: "part4".into(), slot: Slot::B },
            Partition::ZBI { name: "part5".into(), slot: Slot::R },
            Partition::VBMeta { name: "part6".into(), slot: Slot::R },
            Partition::FVM { name: "part7".into() },
        ];
        let image_map: ImageMap = btreemap! {
            Slot::A => btreemap!{
                ImageType::ZBI => "zbi_a".into(),
                ImageType::VBMeta => "vbmeta_a".into(),
                ImageType::FVM => "fvm_a".into(),
            },
            Slot::B => btreemap!{
                ImageType::ZBI => "zbi_b".into(),
                ImageType::VBMeta => "vbmeta_b".into(),
                ImageType::FVM => "fvm_b".into(),
            },
            Slot::R => btreemap!{
                ImageType::ZBI => "zbi_r".into(),
                ImageType::VBMeta => "vbmeta_r".into(),
                ImageType::FVM => "fvm_r".into(),
            },
        };
        let mapped = get_mapped_partitions(&partitions, &image_map, /*is_recovery=*/ false);
        assert_eq!(
            mapped,
            vec![
                v3::Partition { name: "part1".into(), path: "zbi_a".into(), condition: None },
                v3::Partition { name: "part2".into(), path: "vbmeta_a".into(), condition: None },
                v3::Partition { name: "part3".into(), path: "zbi_b".into(), condition: None },
                v3::Partition { name: "part4".into(), path: "vbmeta_b".into(), condition: None },
                v3::Partition { name: "part5".into(), path: "zbi_r".into(), condition: None },
                v3::Partition { name: "part6".into(), path: "vbmeta_r".into(), condition: None },
                v3::Partition { name: "part7".into(), path: "fvm_a".into(), condition: None },
            ]
        );
    }

    #[test]
    fn test_get_mapped_partitions_missing_slot() {
        let partitions = vec![
            Partition::ZBI { name: "part1".into(), slot: Slot::A },
            Partition::VBMeta { name: "part2".into(), slot: Slot::A },
            Partition::ZBI { name: "part3".into(), slot: Slot::B },
            Partition::VBMeta { name: "part4".into(), slot: Slot::B },
            Partition::ZBI { name: "part5".into(), slot: Slot::R },
            Partition::VBMeta { name: "part6".into(), slot: Slot::R },
            Partition::FVM { name: "part7".into() },
        ];
        let image_map: ImageMap = btreemap! {
            Slot::A => btreemap!{
                ImageType::ZBI => "zbi_a".into(),
                ImageType::VBMeta => "vbmeta_a".into(),
                ImageType::FVM => "fvm_a".into(),
            },
            Slot::R => btreemap!{
                ImageType::ZBI => "zbi_r".into(),
                ImageType::VBMeta => "vbmeta_r".into(),
            },
        };
        let mapped = get_mapped_partitions(&partitions, &image_map, /*is_recovery=*/ false);
        assert_eq!(
            mapped,
            vec![
                v3::Partition { name: "part1".into(), path: "zbi_a".into(), condition: None },
                v3::Partition { name: "part2".into(), path: "vbmeta_a".into(), condition: None },
                v3::Partition { name: "part5".into(), path: "zbi_r".into(), condition: None },
                v3::Partition { name: "part6".into(), path: "vbmeta_r".into(), condition: None },
                v3::Partition { name: "part7".into(), path: "fvm_a".into(), condition: None },
            ]
        );
    }

    #[test]
    fn test_get_mapped_partitions_recovery() {
        let partitions = vec![
            Partition::ZBI { name: "part1".into(), slot: Slot::A },
            Partition::VBMeta { name: "part2".into(), slot: Slot::A },
            Partition::ZBI { name: "part3".into(), slot: Slot::B },
            Partition::VBMeta { name: "part4".into(), slot: Slot::B },
            Partition::ZBI { name: "part5".into(), slot: Slot::R },
            Partition::VBMeta { name: "part6".into(), slot: Slot::R },
            Partition::FVM { name: "part7".into() },
        ];
        let image_map: ImageMap = btreemap! {
            Slot::A => btreemap!{
                ImageType::ZBI => "zbi_a".into(),
                ImageType::VBMeta => "vbmeta_a".into(),
                ImageType::FVM => "fvm_a".into(),
            },
            Slot::B => btreemap!{
                ImageType::ZBI => "zbi_b".into(),
                ImageType::VBMeta => "vbmeta_b".into(),
                ImageType::FVM => "fvm_b".into(),
            },
            Slot::R => btreemap!{
                ImageType::ZBI => "zbi_r".into(),
                ImageType::VBMeta => "vbmeta_r".into(),
            },
        };
        let mapped = get_mapped_partitions(&partitions, &image_map, /*is_recovery=*/ true);
        assert_eq!(
            mapped,
            vec![
                v3::Partition { name: "part1".into(), path: "zbi_r".into(), condition: None },
                v3::Partition { name: "part2".into(), path: "vbmeta_r".into(), condition: None },
                v3::Partition { name: "part3".into(), path: "zbi_r".into(), condition: None },
                v3::Partition { name: "part4".into(), path: "vbmeta_r".into(), condition: None },
                v3::Partition { name: "part5".into(), path: "zbi_r".into(), condition: None },
                v3::Partition { name: "part6".into(), path: "vbmeta_r".into(), condition: None },
            ]
        );
    }
}
