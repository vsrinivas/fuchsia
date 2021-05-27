// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config::{from_reader, BoardConfig, ProductConfig, VBMetaConfig};
use crate::vfs::RealFilesystemProvider;
use anyhow::{Context, Result};
use assembly_base_package::BasePackageBuilder;
use assembly_update_package::UpdatePackageBuilder;
use ffx_assembly_args::ImageArgs;
use fuchsia_hash::Hash;
use fuchsia_merkle::MerkleTree;
use fuchsia_pkg::{PackageManifest, PackagePath};
use log::info;
use std::fs::{File, OpenOptions};
use std::io::{BufReader, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use vbmeta::Salt;
use zbi::ZbiBuilder;

pub fn assemble(args: ImageArgs) -> Result<()> {
    let ImageArgs { product, board, outdir, gendir, full } = args;

    info!("Loading configuration files.");
    info!("  product:  {}", product.display());
    info!("    board:  {}", board.display());

    let (product, board) = read_configs(product, board)?;
    let gendir = gendir.unwrap_or(outdir.clone());

    let base_merkle = if has_base_package(&product) {
        info!("Creating base package");
        let base_package = construct_base_package(&outdir, &gendir, &product)?;
        let base_merkle = MerkleTree::from_reader(&base_package)
            .context("Failed to calculate the base merkle")?
            .root();
        info!("Base merkle: {}", &base_merkle);
        Some(base_merkle)
    } else {
        info!("Skipping base package creation");
        None
    };

    info!("Creating the ZBI");
    let zbi_path = construct_zbi(&outdir, &gendir, &product, &board, base_merkle)?;

    let vbmeta_path = if let Some(vbmeta_config) = &board.vbmeta {
        info!("Creating the VBMeta image");
        Some(construct_vbmeta(&outdir, vbmeta_config, &zbi_path)?)
    } else {
        None
    };

    // Bail out here for now, unless asked to do otherwise.
    if !full {
        return Ok(());
    }

    info!("Creating the update package");
    let _update_pkg_path =
        construct_update(&outdir, &gendir, &product, &board, &zbi_path, vbmeta_path, base_merkle)?;

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
    base_package.seek(SeekFrom::Start(0))?;
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
    board: &BoardConfig,
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

    // Set the zbi compression to use.
    zbi_builder.set_compression(&board.zbi.compression);

    // Create an output manifest that describes the contents of the built ZBI.
    zbi_builder.set_output_manifest(&gendir.as_ref().join("zbi.json"));

    // Build and return the ZBI.
    let zbi_path = outdir.as_ref().join("fuchsia.zbi");
    zbi_builder.build(gendir, zbi_path.as_path())?;
    Ok(zbi_path)
}

fn construct_vbmeta(
    outdir: impl AsRef<Path>,
    vbmeta: &VBMetaConfig,
    zbi: impl AsRef<Path>,
) -> Result<PathBuf> {
    // Generate the salt, or use one provided by the board.
    let salt = match &vbmeta.salt {
        Some(salt_path) => {
            let salt_str = std::fs::read_to_string(salt_path)?;
            Salt::decode_hex(&salt_str)?
        }
        _ => Salt::random()?,
    };

    // Sign the image and construct a VBMeta.
    let (vbmeta, _salt) = crate::vbmeta::sign(
        &vbmeta.kernel_partition,
        zbi,
        &vbmeta.key,
        &vbmeta.key_metadata,
        vbmeta.additional_descriptor_files.clone(),
        salt,
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
    vbmeta: Option<impl AsRef<Path>>,
    base_merkle: Option<Hash>,
) -> Result<PathBuf> {
    // Create the board name file.
    // TODO(fxbug.dev/76326): Create a better system for writing intermediate files.
    let board_name = gendir.as_ref().join("board");
    std::fs::write(&board_name, &board.board_name)?;

    // Add several files to the update package.
    let mut update_pkg_builder = UpdatePackageBuilder::new();
    if let Some(epoch_file) = &product.epoch_file {
        update_pkg_builder.add_file(epoch_file, "epoch.json")?;
    }
    if let Some(version_file) = &product.version_file {
        update_pkg_builder.add_file(version_file, "version")?;
    }
    update_pkg_builder.add_file(&board_name, "board")?;
    update_pkg_builder.add_file(zbi, "zbi")?;

    if let Some(vbmeta) = vbmeta {
        update_pkg_builder.add_file(vbmeta, "fuchsia.vbmeta")?;
    }

    if let Some(recovery_config) = &board.recovery {
        update_pkg_builder.add_file(&recovery_config.zbi, "zedboot")?;
        update_pkg_builder.add_file(&recovery_config.vbmeta, "recovery.vbmeta")?;
    }

    // Add the bootloaders.
    for bootloader in &board.bootloaders {
        update_pkg_builder.add_file(&bootloader.source, &bootloader.name)?;
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

    if let Some(base_merkle) = base_merkle {
        // Add the base package merkle.
        // TODO(fxbug.dev/76986): Do not hardcode the base package path.
        update_pkg_builder
            .add_package(PackagePath::from_name_and_variant("system_image", "0")?, base_merkle)?;
    }

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
