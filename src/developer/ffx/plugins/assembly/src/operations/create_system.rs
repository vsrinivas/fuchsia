// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::util;

use anyhow::{Context, Result};
use assembly_images_config::ImagesConfig;
use assembly_tool::testing::FakeToolProvider;
use assembly_tool::ToolProvider;
use ffx_assembly_args::CreateSystemArgs;
use std::fs::File;

pub fn create_system(args: CreateSystemArgs) -> Result<()> {
    let CreateSystemArgs { images, outdir, gendir } = args;
    let _gendir = gendir.unwrap_or(outdir.clone());

    let _images_config: ImagesConfig =
        util::read_config(images).context("Failed to read the images config")?;

    // Get the tool set.
    let sdk_tools = FakeToolProvider::default();

    // Write the tool command log.
    let command_log_path = outdir.join("new_commands.json");
    let command_log =
        File::create(command_log_path).context("Failed to create command_log.json")?;
    serde_json::to_writer(&command_log, sdk_tools.log())
        .context("Failed to write the command log")?;

    Ok(())
}
