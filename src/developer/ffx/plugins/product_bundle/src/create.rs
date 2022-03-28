// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Command to create a Product Bundle Metadata (PBM) file from command
//! line args pass in through `CreateCommand`.

use {
    anyhow::{anyhow, Result},
    errors::ffx_error,
    ffx_product_bundle_args::{CreateCommand, ProductBundleType},
    pathdiff::diff_paths,
    sdk_metadata::{
        ElementType, EmuManifest, Envelope, FlashManifest, ImageBundle, Manifests, MetadataValue,
        PackageBundle, ProductBundleV1,
    },
    serde::{Deserialize, Serialize},
    std::fs::{File, OpenOptions},
    std::io::BufReader,
    std::path::Path,
    std::path::PathBuf,
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
    let images_vec = vec![ImageBundle {
        base_uri: path_relative_to_dir(&cmd.images, &cmd.out),
        format: "files".to_owned(),
    }];
    let packages_vec = vec![PackageBundle {
        repo_uri: path_relative_to_dir(&cmd.packages, &cmd.out),
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
        manifests.emu = Some(create_product_bundle_for_emu(
            &cmd.disk_image,
            &cmd.zbi,
            &cmd.multiboot_bin,
            &cmd.images,
        ));
    }
    if cmd.types.contains(ProductBundleType::FLASH) {
        manifests.flash = Some(create_product_bundle_for_flash(&cmd.flash_manifest)?);
    };

    let product_bundle = ProductBundleV1 {
        description,
        name,
        device_refs,
        images: images_vec,
        packages: packages_vec,
        manifests: Some(manifests),
        metadata: Some(metadata),
        kind: ElementType::ProductBundle,
    };

    let file =
        OpenOptions::new().create(true).write(true).truncate(true).open(cmd.out.clone()).map_err(
            |e| ffx_error!(r#"Cannot create product bundle file "{}": {}"#, cmd.out.display(), e),
        )?;
    let envelope = Envelope::<ProductBundleV1>::from(product_bundle)?;

    serde_json::to_writer_pretty(&file, &envelope)?;

    Ok(())
}

fn create_product_bundle_for_emu(
    disk_image: &str,
    zbi: &str,
    multiboot_bin: &str,
    images: &str,
) -> EmuManifest {
    EmuManifest {
        disk_images: vec![path_relative_to_dir(disk_image, images)],
        initial_ramdisk: path_relative_to_dir(zbi, images),
        kernel: path_relative_to_dir(multiboot_bin, images),
    }
}

fn create_product_bundle_for_flash(flash_manifest: &str) -> Result<FlashManifest> {
    let flash: FlashManifest =
        File::open(flash_manifest).map(BufReader::new).map(serde_json::from_reader)??;
    Ok(flash)
}

/// Rebase |path| onto |dir| even if |dir| is not in the current working directory.
fn path_relative_to_dir<P, Q>(path: P, dir: Q) -> String
where
    P: AsRef<Path> + std::fmt::Debug,
    Q: AsRef<Path> + std::fmt::Debug,
{
    // Rebase the paths.
    let path = diff_paths(&path, &dir)
        .ok_or(anyhow!("Failed to get relative path for file: {:?}", path))
        .unwrap_or(PathBuf::from(path.as_ref()));

    path.display().to_string()
}

#[cfg(test)]
mod test {
    use super::*;
    use ffx_product_bundle_args::ProductBundleTypes;
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
        let out_filename = root.join("product_bundle.json");
        let build_info_path = root.join("build_info.json");
        create_build_info(build_info_path.clone());

        let cmd = CreateCommand {
            types: ProductBundleTypes { types: vec![ProductBundleType::EMU] },
            packages: String::from("../amber-files"),
            images: String::from("../images"),
            multiboot_bin: String::from("../images/multiboot.bin"),
            device_name: String::from("x64"),
            disk_image: String::from("../images/fvm.blk"),
            zbi: String::from("../images/fuchsia.zbi"),
            build_info: build_info_path.into_os_string().into_string().unwrap(),
            flash_manifest: String::from(""),
            out: out_filename.clone(),
        };
        create_product_bundle(&cmd).await.unwrap();
        let envelope: Envelope<ProductBundleV1> = File::open(&out_filename)
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
        let out_filename = root.join("product_bundle.json");

        let flash_manifest_path = root.join("flash_manifest.json");
        create_flash_manifest(flash_manifest_path.clone());

        let build_info_path = root.join("build_info.json");
        create_build_info(build_info_path.clone());

        let cmd = CreateCommand {
            types: ProductBundleTypes { types: vec![ProductBundleType::FLASH] },
            packages: String::from("../amber-files"),
            images: String::from("../images"),
            multiboot_bin: String::from("../images/multiboot.bin"),
            device_name: String::from("x64"),
            disk_image: String::from("../images/fvm.blk"),
            zbi: String::from("../images/fuchsia.zbi"),
            build_info: build_info_path.into_os_string().into_string().unwrap(),
            flash_manifest: flash_manifest_path.into_os_string().into_string().unwrap(),
            out: out_filename.clone(),
        };
        create_product_bundle(&cmd).await.unwrap();
        let envelope: Envelope<ProductBundleV1> = File::open(&out_filename)
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
