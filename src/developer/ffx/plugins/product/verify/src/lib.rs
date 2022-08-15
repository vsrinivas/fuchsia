// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Verifier methods that ensure that a downloaded product bundle is a valid and ready to use
//! format.
//! TODO(fxbug.dev/106850): Investigate whether using the validator Rust crate helps simplify this
//! logic.

use anyhow::{bail, Context, Result};
use ffx_core::ffx_plugin;
use ffx_product_verify_args::VerifyCommand;
use sdk_metadata::{ElementType, Envelope, ProductBundleV1};
use std::fs::{self, File};

/// Verify that the product bundle has the correct format and is ready for use.
#[ffx_plugin("product.experimental")]
fn pb_verify(cmd: VerifyCommand) -> Result<()> {
    let file = File::open(&cmd.product_bundle).context("opening product bundle")?;
    let envelope: Envelope<ProductBundleV1> =
        serde_json::from_reader(file).context("parsing product bundle")?;
    if let Some(verified_path) = &cmd.verified_file {
        fs::write(verified_path, "verified").context("writing verified file")?;
    }
    pb_verify_product_bundle(envelope.data)
}

fn pb_verify_product_bundle(product_bundle: ProductBundleV1) -> Result<()> {
    // TODO(https://fxbug.dev/82728): Add path validation.
    if product_bundle.kind != ElementType::ProductBundle {
        bail!("File type is not ProductBundle");
    }
    if product_bundle.device_refs.is_empty() {
        bail!("At least one 'device_ref' must be supplied");
    }
    if product_bundle.images.is_empty() {
        bail!("At least one entry in 'images' must be supplied");
    }
    for image in product_bundle.images {
        if image.format != "files" && image.format != "tgz" {
            bail!("Only images with format 'files' or 'tgz' are supported");
        }
        if !image.base_uri.starts_with("file:") {
            bail!("Image 'base_uri' paths must start with 'file:'");
        }
    }
    if product_bundle.packages.is_empty() {
        bail!("At least one entry in 'packages' must be supplied");
    }
    for package in product_bundle.packages {
        if package.format != "files" && package.format != "tgz" {
            bail!("Only packages with format 'files' or 'tgz' are supported");
        }
        if let Some(blob_uri) = package.blob_uri {
            if !blob_uri.starts_with("file:") {
                bail!("Package 'blob_uri' paths must start with 'file:'");
            }
        }
        if !package.repo_uri.starts_with("file:") {
            bail!("Package 'repo_uri' paths must start with 'file:'");
        }
    }
    if let Some(emu) = product_bundle.manifests.emu {
        if emu.disk_images.is_empty() {
            bail!("At least one entry in the emulator 'disk_images' must be supplied");
        }
    }
    if let Some(flash) = product_bundle.manifests.flash {
        if flash.products.is_empty() {
            bail!("At least one entry in the flash manifest 'products' must be supplied");
        }
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use sdk_metadata::{
        ElementType, EmuManifest, FlashManifest, ImageBundle, Manifests, PackageBundle, Product,
        ProductBundleV1,
    };

    #[test]
    fn verify_valid() {
        let pb = default_valid_pb();
        assert!(pb_verify_product_bundle(pb).is_ok());
    }

    #[test]
    fn verify_invalid_type() {
        let mut pb = default_valid_pb();
        pb.kind = ElementType::PhysicalDevice;
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_missing_device_ref() {
        let mut pb = default_valid_pb();
        pb.device_refs = vec![];
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_missing_images() {
        let mut pb = default_valid_pb();
        pb.images = vec![];
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_image_invalid_format() {
        let mut pb = default_valid_pb();
        pb.images[0].format = "invalid".into();
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_image_invalid_base_uri() {
        let mut pb = default_valid_pb();
        pb.images[0].base_uri = "gs://path/to/file".into();
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_missing_packages() {
        let mut pb = default_valid_pb();
        pb.packages = vec![];
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_package_invalid_format() {
        let mut pb = default_valid_pb();
        pb.packages[0].format = "invalid".into();
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_package_invalid_blob_uri() {
        let mut pb = default_valid_pb();
        pb.packages[0].blob_uri = Some("gs://path/to/file".into());
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_package_invalid_repo_uri() {
        let mut pb = default_valid_pb();
        pb.packages[0].repo_uri = "gs://path/to/file".into();
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_missing_emu_disk_images() {
        let mut pb = default_valid_pb();
        pb.manifests.emu = Some(EmuManifest {
            disk_images: vec![],
            initial_ramdisk: "ramdisk".into(),
            kernel: "kernel".into(),
        });
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_missing_flash_products() {
        let mut pb = default_valid_pb();
        pb.manifests.flash = Some(FlashManifest {
            hw_revision: "board".into(),
            products: vec![],
            credentials: vec![],
        });
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    fn default_valid_pb() -> ProductBundleV1 {
        ProductBundleV1 {
            description: None,
            device_refs: vec!["device".into()],
            images: vec![ImageBundle {
                base_uri: "file://path/to/images".into(),
                format: "files".into(),
            }],
            manifests: Manifests {
                emu: Some(EmuManifest {
                    disk_images: vec!["file://path/to/images".into()],
                    initial_ramdisk: "ramdisk".into(),
                    kernel: "kernel".into(),
                }),
                flash: Some(FlashManifest {
                    hw_revision: "board".into(),
                    products: vec![Product {
                        bootloader_partitions: vec![],
                        name: "product".into(),
                        oem_files: vec![],
                        partitions: vec![],
                        requires_unlock: false,
                    }],
                    credentials: vec![],
                }),
            },
            metadata: None,
            packages: vec![PackageBundle {
                blob_uri: Some("file://path/to/blobs".into()),
                format: "files".into(),
                repo_uri: "file://path/to/repo".into(),
            }],
            name: "default_pb".into(),
            kind: ElementType::ProductBundle,
        }
    }
}
