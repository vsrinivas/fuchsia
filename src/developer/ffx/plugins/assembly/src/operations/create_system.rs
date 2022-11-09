// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base_package::{construct_base_package, BasePackage};
use crate::fvm::construct_fvm;
use crate::util;
use crate::vbmeta;
use crate::zbi;

use anyhow::{anyhow, Context, Result};
use assembly_config_schema::ImageAssemblyConfig;
use assembly_images_config::{Fvm, Image, ImagesConfig, VBMeta, Zbi};
use assembly_manifest::AssemblyManifest;
use assembly_tool::{SdkToolProvider, ToolProvider};
use assembly_update_packages_manifest::UpdatePackagesManifest;
use camino::{Utf8Path, Utf8PathBuf};
use ffx_assembly_args::{CreateSystemArgs, PackageMode};
use fuchsia_pkg::{PackageManifest, PackagePath};
use serde_json::ser;
use std::collections::BTreeSet;
use std::fs::File;
use tracing::info;

pub fn create_system(args: CreateSystemArgs) -> Result<()> {
    let CreateSystemArgs { image_assembly_config, images, outdir, gendir, base_package_name, mode } =
        args;
    let gendir = gendir.unwrap_or(outdir.clone());
    let base_package_name = base_package_name.unwrap_or("system_image".to_string());

    let image_assembly_config: ImageAssemblyConfig = util::read_config(image_assembly_config)
        .context("Failed to read the image assembly config")?;
    let images_config: ImagesConfig =
        util::read_config(images).context("Failed to read the images config")?;

    // Get the tool set.
    let tools = SdkToolProvider::try_new()?;

    let mut assembly_manifest = AssemblyManifest::default();
    assembly_manifest
        .images
        .push(assembly_manifest::Image::QemuKernel(image_assembly_config.qemu_kernel.clone()));

    // Create the base package if needed.
    let base_package = if has_base_package(&image_assembly_config) {
        info!("Creating base package");
        Some(construct_base_package(
            &mut assembly_manifest,
            &outdir,
            &gendir,
            &base_package_name,
            &image_assembly_config,
        )?)
    } else {
        info!("Skipping base package creation");
        None
    };

    // Get the FVM config.
    let fvm_config: Option<&Fvm> = images_config.images.iter().find_map(|i| match i {
        Image::Fvm(fvm) => Some(fvm),
        _ => None,
    });

    // Create all the filesystems and FVMs.
    if let Some(fvm_config) = fvm_config {
        // TODO: warn if bootfs_only mode
        if let Some(base_package) = &base_package {
            construct_fvm(
                &outdir,
                &gendir,
                &tools,
                &mut assembly_manifest,
                &image_assembly_config,
                fvm_config.clone(),
                &base_package,
            )?;
        }
    } else {
        info!("Skipping fvm creation");
    };

    // Find the first standard FVM that was generated.
    let fvm_for_zbi: Option<Utf8PathBuf> = match &mode {
        PackageMode::FvmInZbi => assembly_manifest.images.iter().find_map(|i| match i {
            assembly_manifest::Image::FVM(path) => Some(path.clone()),
            _ => None,
        }),
        _ => None,
    };

    // Get the ZBI config.
    let zbi_config: Option<&Zbi> = images_config.images.iter().find_map(|i| match i {
        Image::Zbi(zbi) => Some(zbi),
        _ => None,
    });

    let zbi_path: Option<Utf8PathBuf> = if let Some(zbi_config) = zbi_config {
        Some(zbi::construct_zbi(
            tools.get_tool("zbi")?,
            &mut assembly_manifest,
            &outdir,
            &gendir,
            &image_assembly_config,
            &zbi_config,
            base_package.as_ref(),
            fvm_for_zbi,
        )?)
    } else {
        info!("Skipping zbi creation");
        None
    };

    // Building a ZBI is expected, therefore throw an error otherwise.
    let zbi_path = zbi_path.ok_or(anyhow!("Missing a ZBI in the images config"))?;

    // Get the VBMeta config.
    let vbmeta_config: Option<&VBMeta> = images_config.images.iter().find_map(|i| match i {
        Image::VBMeta(vbmeta) => Some(vbmeta),
        _ => None,
    });

    if let Some(vbmeta_config) = vbmeta_config {
        info!("Creating the VBMeta image");
        vbmeta::construct_vbmeta(&mut assembly_manifest, &outdir, &vbmeta_config, &zbi_path)
            .context("Creating the VBMeta image")?;
    } else {
        info!("Skipping vbmeta creation");
    }

    // If the board specifies a vendor-specific signing script, use that to
    // post-process the ZBI.
    if let Some(zbi_config) = zbi_config {
        match &zbi_config.postprocessing_script {
            Some(script) => {
                let signing_tool = tools.get_tool_with_path(script.path.clone())?;
                zbi::vendor_sign_zbi(
                    signing_tool,
                    &mut assembly_manifest,
                    &outdir,
                    &zbi_config,
                    &zbi_path,
                )
                .context("Vendor-signing the ZBI")?;
            }
            _ => {}
        }
    } else {
        info!("Skipping zbi signing");
    }

    // Write the images manifest.
    let images_json_path = outdir.join("images.json");
    let images_json = File::create(images_json_path).context("Creating images manifest")?;
    serde_json::to_writer(images_json, &assembly_manifest).context("Writing images manifest")?;

    // Write the packages manifest.
    create_package_manifest(
        &outdir,
        base_package_name,
        &image_assembly_config,
        base_package.as_ref(),
    )
    .context("Creating the packages manifest")?;

    // Write the tool command log.
    let command_log_path = gendir.join("command_log.json");
    let command_log = File::create(command_log_path).context("Creating command log")?;
    serde_json::to_writer(&command_log, tools.log()).context("Writing command log")?;

    Ok(())
}

fn create_package_manifest(
    outdir: impl AsRef<Utf8Path>,
    base_package_name: impl AsRef<str>,
    assembly_config: &ImageAssemblyConfig,
    base_package: Option<&BasePackage>,
) -> Result<()> {
    let packages_path = outdir.as_ref().join("packages.json");
    let packages_file = File::create(&packages_path).context("Creating the packages manifest")?;
    let mut packages_manifest = UpdatePackagesManifest::V1(BTreeSet::new());
    let mut add_packages_to_update = |packages: &Vec<Utf8PathBuf>| -> Result<()> {
        for package_path in packages {
            let manifest = PackageManifest::try_load_from(package_path)?;
            packages_manifest
                .add_by_manifest(manifest)
                .with_context(|| format!("Adding manifest: {}", package_path))?;
        }
        Ok(())
    };
    add_packages_to_update(&assembly_config.base)?;
    add_packages_to_update(&assembly_config.cache)?;
    if let Some(base_package) = &base_package {
        packages_manifest.add(
            PackagePath::from_name_and_variant(
                base_package_name.as_ref().parse().context("parse package name")?,
                "0".parse().context("parse package variant")?,
            ),
            base_package.merkle,
            None,
        )?;
    }
    Ok(ser::to_writer(packages_file, &packages_manifest).context("Writing packages manifest")?)
}

fn has_base_package(image_assembly_config: &ImageAssemblyConfig) -> bool {
    return !(image_assembly_config.base.is_empty()
        && image_assembly_config.cache.is_empty()
        && image_assembly_config.system.is_empty());
}
