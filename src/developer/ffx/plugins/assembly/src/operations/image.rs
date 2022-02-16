// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base_package::{construct_base_package, BasePackage};
use crate::blobfs;
use crate::config::{BoardConfig, PartialProductConfig, ProductConfig};
use crate::fvm::{construct_fvm, Fvms};
use crate::util;
use crate::vbmeta;
use crate::zbi;

use anyhow::{Context, Result};
use assembly_images_manifest::{Image, ImagesManifest};
use assembly_tool::{SdkToolProvider, ToolProvider};
use assembly_update_packages_manifest::UpdatePackagesManifest;
use ffx_assembly_args::ImageArgs;
use fuchsia_pkg::PackagePath;
use log::info;
use serde_json::ser;
use std::collections::BTreeSet;
use std::fs::File;
use std::path::{Path, PathBuf};

pub fn assemble(args: ImageArgs) -> Result<()> {
    let ImageArgs { product: products, board, log_commands, outdir, gendir } = args;

    info!("Loading configuration files.");
    info!("  product:");
    for product in &products {
        info!("    {}", product.display());
    }
    info!("    board:  {}", board.display());

    let (products, board) = read_configs(&products, board)?;
    let product = ProductConfig::try_from_partials(products)?;

    let gendir = gendir.unwrap_or(outdir.clone());

    // Use the sdk to get the host tool paths.
    let sdk_tools = SdkToolProvider::try_new().context("Getting SDK tools")?;

    let base_package: Option<BasePackage> = if has_base_package(&product) {
        info!("Creating base package");
        Some(
            construct_base_package(&outdir, &gendir, &board.base_package_name, &product)
                .context("Creating base package")?,
        )
    } else {
        info!("Skipping base package creation");
        None
    };

    let blobfs_config = blobfs::convert_to_new_config(&board.blobfs)?;
    let blobfs_path: Option<PathBuf> = if let Some(base_package) = &base_package {
        info!("Creating the blobfs");
        Some(
            blobfs::construct_blobfs(
                sdk_tools.get_tool("blobfs")?,
                &outdir,
                &gendir,
                &product,
                &blobfs_config,
                &base_package,
            )
            .context("Creating the blobfs")?,
        )
    } else {
        info!("Skipping blobfs creation");
        None
    };

    let fvms: Option<Fvms> = if let Some(fvm_config) = &board.fvm {
        info!("Creating the fvm");
        Some(
            construct_fvm(&sdk_tools, &outdir, &fvm_config, blobfs_path.as_ref())
                .context("Creating the fvm")?,
        )
    } else {
        info!("Skipping fvm creation");
        None
    };

    // If the FVM should be embedded in the ZBI, select the default one.
    let fvm_for_zbi: Option<&PathBuf> = match (&board.zbi.embed_fvm_in_zbi, &fvms) {
        (true, None) => {
            anyhow::bail!("Config indicates FVM should embed in ZBI, but no FVM was generated");
        }
        (true, Some(fvms)) => Some(&fvms.default),
        (false, _) => None,
    };

    info!("Creating the ZBI");
    let zbi_config = zbi::convert_to_new_config(&board.zbi)?;
    let zbi_path = zbi::construct_zbi(
        sdk_tools.get_tool("zbi")?,
        &outdir,
        &gendir,
        &product,
        &zbi_config,
        base_package.as_ref(),
        fvm_for_zbi,
    )?;

    let vbmeta_path: Option<PathBuf> = if let Some(vbmeta_config) = &board.vbmeta {
        info!("Creating the VBMeta image");
        let vbmeta_config = vbmeta::convert_to_new_config(&board.zbi.name, vbmeta_config)?;
        Some(
            vbmeta::construct_vbmeta(&outdir, &vbmeta_config, &zbi_path)
                .context("Creating the VBMeta image")?,
        )
    } else {
        info!("Skipping vbmeta creation");
        None
    };

    // If the board specifies a vendor-specific signing script, use that to
    // post-process the ZBI, and then use the post-processed ZBI in the update
    // package and the
    let (zbi_for_update_path, signed) = if zbi_config.postprocessing_script.is_some() {
        info!("Vendor signing the ZBI");
        (
            zbi::vendor_sign_zbi(&outdir, &zbi_config, &zbi_path)
                .context("Vendor-signing the ZBI")?,
            true,
        )
    } else {
        (zbi_path, false)
    };

    info!("Creating images manifest");
    let mut images_manifest = ImagesManifest::default();
    images_manifest.images.push(Image::ZBI { path: zbi_for_update_path.clone(), signed: signed });
    if let Some(base_package) = &base_package {
        images_manifest.images.push(Image::BasePackage(base_package.path.clone()));
    }
    if let Some(blobfs_path) = &blobfs_path {
        images_manifest.images.push(Image::BlobFS(blobfs_path.clone()));
    }
    if let Some(fvms) = &fvms {
        images_manifest.images.push(Image::FVM(fvms.default.clone()));
        images_manifest.images.push(Image::FVMSparse(fvms.sparse.clone()));
        images_manifest.images.push(Image::FVMSparseBlob(fvms.sparse_blob.clone()));
        if let Some(path) = &fvms.fastboot {
            images_manifest.images.push(Image::FVMFastboot(path.clone()));
        }
    }
    if let Some(vbmeta_path) = &vbmeta_path {
        images_manifest.images.push(Image::VBMeta(vbmeta_path.clone()));
    }
    let images_json_path = outdir.join("images.json");
    let images_json = File::create(images_json_path).context("Failed to create images.json")?;
    serde_json::to_writer(images_json, &images_manifest)
        .context("Failed to write to images.json")?;

    info!("Creating the packages manifest");
    create_package_manifest(&outdir, &board, &product, base_package.as_ref())
        .context("Creating the packages manifest")?;

    if log_commands {
        let command_log_path = gendir.join("command_log.json");
        let command_log =
            File::create(command_log_path).context("Failed to create command_log.json")?;
        serde_json::to_writer(&command_log, sdk_tools.log())
            .context("Failed to write to command_log.json")?;
    }

    Ok(())
}

fn create_package_manifest(
    outdir: impl AsRef<Path>,
    board: &BoardConfig,
    product: &ProductConfig,
    base_package: Option<&BasePackage>,
) -> Result<()> {
    let packages_path = outdir.as_ref().join("packages.json");
    let packages_file = File::create(&packages_path).context("Failed to create packages.json")?;
    let mut packages_manifest = UpdatePackagesManifest::V1(BTreeSet::new());
    let mut add_packages_to_update = |packages: &Vec<PathBuf>| -> Result<()> {
        for package_path in packages {
            let manifest = util::pkg_manifest_from_path(package_path)?;
            packages_manifest
                .add_by_manifest(manifest)
                .context(format!("Adding manifest: {}", package_path.display()))?;
        }
        Ok(())
    };
    add_packages_to_update(&product.base)?;
    add_packages_to_update(&product.cache)?;
    if let Some(base_package) = &base_package {
        packages_manifest.add(
            PackagePath::from_name_and_variant(
                board.base_package_name.parse().context("parse package name")?,
                "0".parse().context("parse package variant")?,
            ),
            base_package.merkle,
        )?;
    }
    Ok(ser::to_writer(packages_file, &packages_manifest).context("Writing packages manifest")?)
}

fn read_configs(
    products: &[impl AsRef<Path>],
    board: impl AsRef<Path>,
) -> Result<(Vec<PartialProductConfig>, BoardConfig)> {
    let products = products
        .iter()
        .map(util::read_config)
        .collect::<Result<Vec<PartialProductConfig>>>()
        .context("Unable to parse product configs")?;

    let board: BoardConfig = util::read_config(board).context("Failed to read the board config")?;
    Ok((products, board))
}

fn has_base_package(product: &ProductConfig) -> bool {
    return !(product.base.is_empty() && product.cache.is_empty() && product.system.is_empty());
}
