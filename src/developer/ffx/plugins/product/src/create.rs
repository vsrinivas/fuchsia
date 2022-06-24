// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Command to create a Product Bundle Metadata (PBM) file from command
//! line args pass in through `CreateCommand`.

use {
    anyhow::Result,
    errors::ffx_error,
    ffx_product_args::{CreateCommand, ProductBundleType},
    fs_extra::dir::CopyOptions,
    sdk_metadata::{
        ElementType, EmuManifest, Envelope, FlashManifest, ImageBundle, Manifests, MetadataValue,
        PackageBundle, ProductBundleV1,
    },
    serde::{Deserialize, Serialize},
    std::fs::{create_dir_all, File, OpenOptions},
    std::io::BufReader,
};

/// Description of the build info. This is part of product-bundle metadata.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct BuildInfo {
    pub product: String,
    pub board: String,
    pub version: String,
    pub is_debug: bool,
}

/// Write the product_bundle.json file to the out directory.
/// You can choose to generate the product bundle for emulator or flash by
/// passing in is_emu flag.
pub async fn create_product_bundle(cmd: &CreateCommand) -> Result<()> {
    let build_info: BuildInfo =
        File::open(cmd.build_info.clone()).map(BufReader::new).map(serde_json::from_reader)??;

    let description = Some(
        format!("Product bundle for {}.{}", &build_info.product, &build_info.board).to_owned(),
    );
    let name = format!("{}.{}", &build_info.product, &build_info.board).to_owned();
    let device_refs = vec![cmd.device_name.clone()];
    let images_vec =
        vec![ImageBundle { base_uri: "file:/images".to_owned(), format: "files".to_owned() }];
    let packages_vec = vec![PackageBundle {
        repo_uri: "file:/packages".to_owned(),
        format: "files".to_owned(),
        blob_uri: None,
    }];
    let metadata = vec![
        ("build_info_board".to_owned(), MetadataValue::StringValue(build_info.board.clone())),
        ("build_info_product".to_owned(), MetadataValue::StringValue(build_info.product.clone())),
        ("build_info_version".to_owned(), MetadataValue::StringValue(build_info.version.clone())),
        ("is_debug".to_owned(), MetadataValue::Boolean(build_info.is_debug.clone())),
    ];

    let mut manifests = Manifests::default();
    if cmd.types.contains(ProductBundleType::EMU) {
        manifests.emu = Some(EmuManifest {
            disk_images: vec!["fvm.blk".to_owned()],
            initial_ramdisk: "fuchsia.zbi".to_owned(),
            kernel: "multiboot.bin".to_owned(),
        });
    }
    if cmd.types.contains(ProductBundleType::FLASH) {
        manifests.flash = Some(create_product_bundle_for_flash(&cmd.flash_manifest)?);
    };

    let product_bundle = ProductBundleV1 {
        description,
        name: name.clone(),
        device_refs,
        images: images_vec,
        packages: packages_vec,
        manifests: Some(manifests),
        metadata: Some(metadata),
        kind: ElementType::ProductBundle,
    };

    let product_dir = cmd.out.join(&name);
    let image_dir = product_dir.join("image");
    let package_dir = product_dir.join("packages");
    create_dir_all(&image_dir)?;
    create_dir_all(&package_dir)?;
    let file = OpenOptions::new()
        .create(true)
        .write(true)
        .truncate(true)
        .open(cmd.out.join("product_bundle.json"))
        .map_err(|e| {
            ffx_error!(r#"Cannot create product bundle file "{}": {}"#, cmd.out.display(), e)
        })?;
    let envelope = Envelope::<ProductBundleV1>::from(product_bundle)?;

    serde_json::to_writer_pretty(&file, &envelope)?;

    // Copy images and package directory to the output directory.
    let options = CopyOptions { copy_inside: true, ..CopyOptions::new() };
    fs_extra::copy_items(&vec![&cmd.images], &image_dir, &options)?;
    fs_extra::copy_items(&vec![&cmd.packages], &package_dir, &options)?;
    fs_extra::copy_items(&vec![cmd.multiboot_bin.clone()], &image_dir, &options)?;

    Ok(())
}

fn create_product_bundle_for_flash(flash_manifest: &str) -> Result<FlashManifest> {
    let flash: FlashManifest =
        File::open(flash_manifest).map(BufReader::new).map(serde_json::from_reader)??;
    Ok(flash)
}

#[cfg(test)]
mod test {
    use super::*;
    use ffx_product_args::ProductBundleTypes;
    use std::io::Write;
    use std::path::PathBuf;

    fn write_file(path: PathBuf, body: &[u8]) {
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        tmp.write_all(body).unwrap();
        tmp.persist(path).unwrap();
    }

    fn create_build_info(build_info_path: PathBuf) {
        let data = r#"
        {
            "product":"workstation-oot",
            "board": "x64",
            "version": "6.0.0.1",
            "is_debug": false
        }
        "#;
        write_file(build_info_path, data.as_bytes());
    }

    fn create_flash_manifest(flash_manifest: PathBuf) {
        let data = r#"
        {
            "hw_revision": "x64",
            "products": [{
                "bootloader_partitions": [],
                "name": "fuchsia",
                "oem_files": [],
                "partitions": [
                    {
                        "name": "",
                        "path": "fuchsia.zbi"
                    },
                    {
                        "name": "",
                        "path": "zedboot.zbi"
                    },
                    {
                        "name": "",
                        "path": "fuchsia.vbmeta"
                    },
                    {
                        "name": "",
                        "path": "zedboot.vbmeta"
                    }
                ]}
            ]
        }
        "#;
        write_file(flash_manifest, data.as_bytes());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_pbm_for_emu() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();
        let out_dir = root.join("out");
        let build_info_path = root.join("build_info.json");
        create_build_info(build_info_path.clone());
        let images = root.join("images");
        let packages = root.join("amber-files");
        create_dir_all(&images).unwrap();
        create_dir_all(&packages).unwrap();
        let multiboot_bin = images.join("multiboot.bin");
        write_file(multiboot_bin.clone(), "".as_bytes());

        let cmd = CreateCommand {
            types: ProductBundleTypes { types: vec![ProductBundleType::EMU] },
            packages: packages.into_os_string().into_string().unwrap(),
            images: images.into_os_string().into_string().unwrap(),
            multiboot_bin: multiboot_bin.into_os_string().into_string().unwrap(),
            device_name: String::from("x64"),
            build_info: build_info_path.into_os_string().into_string().unwrap(),
            flash_manifest: String::from(""),
            out: out_dir.clone(),
        };
        create_product_bundle(&cmd).await.unwrap();
        let envelope: Envelope<ProductBundleV1> = File::open(&out_dir.join("product_bundle.json"))
            .map(BufReader::new)
            .map(serde_json::from_reader)
            .unwrap()
            .unwrap();

        let product_bundle: ProductBundleV1 = envelope.data;

        assert_eq!("Product bundle for workstation-oot.x64", product_bundle.description.unwrap());
        assert_eq!("workstation-oot.x64", product_bundle.name);
        assert!(product_bundle.manifests.clone().unwrap().emu.is_some());
        assert!(product_bundle.manifests.clone().unwrap().flash.is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_pbm_for_flash() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();
        let out_dir = root.join("out");
        let images = root.join("images");
        let packages = root.join("amber-files");
        create_dir_all(&images).unwrap();
        create_dir_all(&packages).unwrap();
        let multiboot_bin = images.join("multiboot.bin");
        write_file(multiboot_bin.clone(), "".as_bytes());

        let flash_manifest_path = root.join("flash_manifest.json");
        create_flash_manifest(flash_manifest_path.clone());

        let build_info_path = root.join("build_info.json");
        create_build_info(build_info_path.clone());

        let cmd = CreateCommand {
            types: ProductBundleTypes { types: vec![ProductBundleType::FLASH] },
            packages: packages.into_os_string().into_string().unwrap(),
            images: images.into_os_string().into_string().unwrap(),
            multiboot_bin: multiboot_bin.into_os_string().into_string().unwrap(),
            device_name: String::from("x64"),
            build_info: build_info_path.into_os_string().into_string().unwrap(),
            flash_manifest: flash_manifest_path.into_os_string().into_string().unwrap(),
            out: out_dir.clone(),
        };
        create_product_bundle(&cmd).await.unwrap();
        let envelope: Envelope<ProductBundleV1> = File::open(&out_dir.join("product_bundle.json"))
            .map(BufReader::new)
            .map(serde_json::from_reader)
            .unwrap()
            .unwrap();

        let product_bundle: ProductBundleV1 = envelope.data;
        assert_eq!("Product bundle for workstation-oot.x64", product_bundle.description.unwrap());
        assert_eq!("workstation-oot.x64", product_bundle.name);
        assert!(product_bundle.manifests.clone().unwrap().flash.is_some());
        assert!(product_bundle.manifests.clone().unwrap().emu.is_none());
    }
}
