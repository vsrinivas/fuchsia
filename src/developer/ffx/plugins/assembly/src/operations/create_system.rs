// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base_package::{construct_base_package, BasePackage};
use crate::fvm_new::construct_fvm;
use crate::util;

use anyhow::{Context, Result};
use assembly_config::ImageAssemblyConfig;
use assembly_images_config::{Fvm, Image, ImagesConfig};
use assembly_tool::testing::FakeToolProvider;
use assembly_tool::ToolProvider;
use ffx_assembly_args::CreateSystemArgs;
use log::info;
use std::fs::File;

pub fn create_system(args: CreateSystemArgs) -> Result<()> {
    let CreateSystemArgs { image_assembly_config, images, outdir, gendir, base_package_name } =
        args;
    let gendir = gendir.unwrap_or(outdir.clone());
    let base_package_name = base_package_name.unwrap_or("system_image".to_string());

    let image_assembly_config: ImageAssemblyConfig = util::read_config(image_assembly_config)
        .context("Failed to read the image assembly config")?;
    let images_config: ImagesConfig =
        util::read_config(images).context("Failed to read the images config")?;

    // Get the tool set.
    let tools = FakeToolProvider::default();

    // 1. Create the base package if needed.
    let base_package: Option<BasePackage> = if has_base_package(&image_assembly_config) {
        info!("Creating base package");
        Some(construct_base_package(&outdir, &gendir, &base_package_name, &image_assembly_config)?)
    } else {
        info!("Skipping base package creation");
        None
    };

    // Get the FVM config.
    let fvm_config: Option<&Fvm> = images_config.images.iter().find_map(|i| match i {
        Image::Fvm(fvm) => Some(fvm),
        _ => None,
    });

    // 2. Create all the filesystems and FVMs.
    if let Some(fvm_config) = fvm_config {
        // TODO: warn if bootfs_only mode
        if let Some(base_package) = &base_package {
            construct_fvm(
                &outdir,
                &gendir,
                &tools,
                &image_assembly_config,
                fvm_config.clone(),
                &base_package,
            )?;
        }
    } else {
        info!("Skipping fvm creation");
    };

    // Write the tool command log.
    let command_log_path = gendir.join("command_log.json");
    let command_log =
        File::create(command_log_path).context("Failed to create command_log.json")?;
    serde_json::to_writer(&command_log, tools.log()).context("Failed to write the command log")?;

    Ok(())
}

fn has_base_package(image_assembly_config: &ImageAssemblyConfig) -> bool {
    return !(image_assembly_config.base.is_empty()
        && image_assembly_config.cache.is_empty()
        && image_assembly_config.system.is_empty());
}
