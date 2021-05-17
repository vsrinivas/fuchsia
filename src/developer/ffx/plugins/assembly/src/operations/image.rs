// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config::{from_reader, BoardConfig, ProductConfig};
use crate::vfs::RealFilesystemProvider;
use anyhow::{Context, Result};
use assembly_base_package::BasePackageBuilder;
use assembly_update_package::UpdatePackageBuilder;
use ffx_assembly_args::ImageArgs;
use fuchsia_hash::Hash;
use fuchsia_merkle::MerkleTree;
use fuchsia_pkg::{PackageManifest, PackagePath};
use std::fs::{File, OpenOptions};
use std::io::BufReader;
use std::path::{Path, PathBuf};
use vbmeta::Salt;
use zbi::ZbiBuilder;

pub fn assemble(args: ImageArgs) -> Result<()> {
    let ImageArgs { product, board, outdir, gendir, full } = args;

    let (product, board) = read_configs(product, board)?;
    let gendir = gendir.unwrap_or(outdir.clone());

    let base_package = construct_base_package(&outdir, &gendir, &product)?;
    let base_merkle = MerkleTree::from_reader(&base_package)
        .context("Failed to calculate the base merkle")?
        .root();
    println!("Base merkle: {}", &base_merkle);

    if !full {
        return Ok(());
    }

    let zbi_path = construct_zbi(&outdir, &gendir, &product, Some(base_merkle))?;
    let vbmeta_path = construct_vbmeta(&outdir, &board, &zbi_path)?;
    let _update_pkg_path =
        construct_update(&outdir, &gendir, &product, &board, &zbi_path, &vbmeta_path, base_merkle)?;

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

fn construct_base_package(
    outdir: impl AsRef<Path>,
    gendir: impl AsRef<Path>,
    product: &ProductConfig,
) -> Result<File> {
    let mut base_pkg_builder = BasePackageBuilder::default();
    for pkg_manifest_path in &product.extra_packages_for_base_package {
        let pkg_manifest = pkg_manifest_from_path(pkg_manifest_path)?;
        base_pkg_builder.add_files_from_package(pkg_manifest);
    }
    for pkg_manifest_path in &product.base_packages {
        let pkg_manifest = pkg_manifest_from_path(pkg_manifest_path)?;
        base_pkg_builder.add_base_package(pkg_manifest).context(format!(
            "Failed to add package to base package list with manifest: {}",
            pkg_manifest_path.display()
        ))?;
    }
    for pkg_manifest_path in &product.cache_packages {
        let pkg_manifest = pkg_manifest_from_path(pkg_manifest_path)?;
        base_pkg_builder.add_cache_package(pkg_manifest).context(format!(
            "Failed to add package to cache package list with manifest: {}",
            pkg_manifest_path.display()
        ))?;
    }

    let base_package_path = outdir.as_ref().join("base.far");
    let mut base_package = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .open(base_package_path)
        .context("Failed to create the base package file")?;
    let _ = base_pkg_builder
        .build(gendir, &mut base_package)
        .context("Failed to build the base package")?;
    Ok(base_package)
}

fn pkg_manifest_from_path(path: impl AsRef<Path>) -> Result<PackageManifest> {
    let manifest_file = File::open(path)?;
    let pkg_manifest_reader = BufReader::new(manifest_file);
    serde_json::from_reader(pkg_manifest_reader).map_err(Into::into)
}

fn construct_zbi(
    outdir: impl AsRef<Path>,
    gendir: impl AsRef<Path>,
    product: &ProductConfig,
    base_merkle: Option<Hash>,
) -> Result<PathBuf> {
    let mut zbi_builder = ZbiBuilder::default();

    // Add the kernel image.
    zbi_builder.set_kernel(&product.kernel_image);

    // Instruct devmgr that a /system volume is required.
    zbi_builder.add_boot_arg("devmgr.require-system=true");

    // If a base merkle is supplied, then add the boot arguments for startup up pkgfs with the
    // merkle of the Base Package.
    if let Some(base_merkle) = base_merkle {
        // Specify how to launch pkgfs: bin/pkgsvr <base-merkle>
        zbi_builder.add_boot_arg(&format!("zircon.system.pkgfs.cmd=bin/pkgsvr+{}", base_merkle));

        // Add the pkgfs blobs to the boot arguments, so that pkgfs can be bootstrapped out of blobfs,
        // before the blobfs service is available.
        let pkgfs_manifest: PackageManifest = product
            .base_packages
            .iter()
            .find_map(|p| {
                if let Ok(m) = pkg_manifest_from_path(p) {
                    if m.name() == "pkgfs" {
                        return Some(m);
                    }
                }
                return None;
            })
            .context("Failed to find pkgfs in the base packages")?;

        pkgfs_manifest.into_blobs().into_iter().filter(|b| b.path != "meta/").for_each(|b| {
            zbi_builder.add_boot_arg(&format!("zircon.system.pkgfs.file.{}={}", b.path, b.merkle));
        });
    }

    // Add the command line.
    for cmd in &product.kernel_cmdline {
        zbi_builder.add_cmdline_arg(cmd);
    }

    // Add the BootFS files.
    for bootfs_entry in &product.bootfs_files {
        zbi_builder.add_bootfs_file(&bootfs_entry.source, &bootfs_entry.destination);
    }

    // Build and return the ZBI.
    let zbi_path = outdir.as_ref().join("fuchsia.zbi");
    zbi_builder.build(gendir, zbi_path.as_path())?;
    Ok(zbi_path)
}

fn construct_vbmeta(
    outdir: impl AsRef<Path>,
    board: &BoardConfig,
    zbi: impl AsRef<Path>,
) -> Result<PathBuf> {
    // Sign the image and construct a VBMeta.
    let (vbmeta, _salt) = crate::vbmeta::sign(
        &board.vbmeta.kernel_partition,
        zbi,
        &board.vbmeta.key,
        &board.vbmeta.key_metadata,
        board.vbmeta.additional_descriptor.clone(),
        Salt::random()?,
        &RealFilesystemProvider {},
    )?;

    // Write VBMeta to a file and return the path.
    let vbmeta_path = outdir.as_ref().join("fuchsia.vbmeta");
    std::fs::write(&vbmeta_path, vbmeta.as_bytes())?;
    Ok(vbmeta_path)
}

fn construct_update(
    outdir: impl AsRef<Path>,
    gendir: impl AsRef<Path>,
    product: &ProductConfig,
    board: &BoardConfig,
    zbi: impl AsRef<Path>,
    vbmeta: impl AsRef<Path>,
    base_merkle: Hash,
) -> Result<PathBuf> {
    // Create the board name file.
    // TODO(fxbug.dev/76326): Create a better system for writing intermediate files.
    let board_name = gendir.as_ref().join("board");
    std::fs::write(&board_name, &board.board_name)?;

    // Add several files to the update package.
    let mut update_pkg_builder = UpdatePackageBuilder::new();
    update_pkg_builder.add_file(&product.epoch_file, "epoch.json")?;
    update_pkg_builder.add_file(&product.version_file, "version")?;
    update_pkg_builder.add_file(&board_name, "board")?;
    update_pkg_builder.add_file(zbi, "zbi")?;
    update_pkg_builder.add_file(vbmeta, "fuchsia.vbmeta")?;
    update_pkg_builder.add_file(&board.recovery.zbi, "zedboot")?;
    update_pkg_builder.add_file(&board.recovery.vbmeta, "recovery.vbmeta")?;

    // Add the bootloaders.
    for bootloader in &board.bootloaders {
        update_pkg_builder
            .add_file(&bootloader.source, format!("firmware_{}", bootloader.bootloader_type))?;
    }

    // Add the packages that need to be updated.
    let mut add_packages_to_update = |packages: &Vec<PathBuf>| -> Result<()> {
        for package_path in packages {
            let manifest = pkg_manifest_from_path(package_path)?;
            update_pkg_builder.add_package_by_manifest(manifest)?;
        }
        Ok(())
    };
    add_packages_to_update(&product.base_packages)?;
    add_packages_to_update(&product.cache_packages)?;

    // Add the base package merkle.
    // TODO(fxbug.dev/76986): Do not hardcode the base package path.
    update_pkg_builder
        .add_package(PackagePath::from_name_and_variant("system_image", "0")?, base_merkle)?;

    // Build the update package and return its path.
    let update_package_path = outdir.as_ref().join("update.far");
    let mut update_package = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .open(&update_package_path)
        .context("Failed to create the update package file")?;
    update_pkg_builder
        .build(gendir, &mut update_package)
        .context("Failed to build the update package")?;
    Ok(update_package_path)
}
