// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base_package::{construct_base_package, BasePackage};
use crate::blobfs::construct_blobfs;
use crate::config::{from_reader, BoardConfig, ProductConfig};
use crate::fvm::{construct_fvm, Fvms};
use crate::update_package::{construct_update, UpdatePackage};
use crate::vbmeta::construct_vbmeta;
use crate::zbi::{construct_zbi, vendor_sign_zbi};

use anyhow::{Context, Result};
use ffx_assembly_args::ImageArgs;
use ffx_config::get_sdk;
use futures::executor::block_on;
use log::info;
use std::fs::File;
use std::path::{Path, PathBuf};

pub fn assemble(args: ImageArgs) -> Result<()> {
    let ImageArgs { product, board, outdir, gendir, full: _ } = args;

    info!("Loading configuration files.");
    info!("  product:  {}", product.display());
    info!("    board:  {}", board.display());

    let (product, board) = read_configs(product, board)?;
    let gendir = gendir.unwrap_or(outdir.clone());

    // Use the sdk to get the host tool paths.
    let sdk = block_on(get_sdk())?;

    let base_package: Option<BasePackage> = if has_base_package(&product) {
        info!("Creating base package");
        Some(construct_base_package(&outdir, &gendir, &product)?)
    } else {
        info!("Skipping base package creation");
        None
    };

    let blobfs_path: Option<PathBuf> = if let Some(base_package) = &base_package {
        info!("Creating the blobfs");
        Some(construct_blobfs(
            sdk.get_host_tool("blobfs")?,
            &outdir,
            &gendir,
            &product,
            &board.blobfs,
            &base_package,
        )?)
    } else {
        info!("Skipping blobfs creation");
        None
    };

    let fvms: Option<Fvms> = if let Some(fvm_config) = &board.fvm {
        info!("Creating the fvm");
        Some(construct_fvm(
            sdk.get_host_tool("fvm")?,
            sdk.get_host_tool("minfs")?,
            &outdir,
            &fvm_config,
            blobfs_path.as_ref(),
        )?)
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
    let zbi_path = construct_zbi(
        sdk.get_host_tool("zbi")?,
        &outdir,
        &gendir,
        &product,
        &board,
        base_package.as_ref(),
        fvm_for_zbi,
    )?;

    let vbmeta_path: Option<PathBuf> = if let Some(vbmeta_config) = &board.vbmeta {
        info!("Creating the VBMeta image");
        Some(construct_vbmeta(&outdir, &board.zbi.name, vbmeta_config, &zbi_path)?)
    } else {
        info!("Skipping vbmeta creation");
        None
    };

    // If the board specifies a vendor-specific signing script, use that to
    // post-process the ZBI, and then use the post-processed ZBI in the update
    // package and the
    let zbi_for_update_path = if let Some(signing_config) = &board.zbi.signing_script {
        info!("Vendor signing the ZBI");
        vendor_sign_zbi(&outdir, &board, signing_config, &zbi_path)?
    } else {
        zbi_path
    };

    info!("Creating the update package");
    let _update_package: UpdatePackage = construct_update(
        &outdir,
        &gendir,
        &product,
        &board,
        &zbi_for_update_path,
        vbmeta_path,
        base_package.as_ref(),
    )?;

    Ok(())
}

fn read_configs(
    product: impl AsRef<Path>,
    board: impl AsRef<Path>,
) -> Result<(ProductConfig, BoardConfig)> {
    let mut product = File::open(product)?;
    let mut board = File::open(board)?;
    let product: ProductConfig =
        from_reader(&mut product).context("Failed to read the product config")?;
    let board: BoardConfig = from_reader(&mut board).context("Failed to read the board config")?;
    Ok((product, board))
}

fn has_base_package(product: &ProductConfig) -> bool {
    return !(product.base_packages.is_empty()
        && product.cache_packages.is_empty()
        && product.extra_packages_for_base_package.is_empty());
}
