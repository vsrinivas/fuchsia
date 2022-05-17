// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use assembly_partitions_config::PartitionsConfig;
use ffx_assembly_args::CreateFlashManifestArgs;
use std::fs::File;
use std::io::Write;

pub fn create_flash_manifest(args: CreateFlashManifestArgs) -> Result<()> {
    let mut file = File::open(&args.partitions)
        .context(format!("Failed to open: {}", args.partitions.display()))?;
    let _partitions = PartitionsConfig::from_reader(&mut file)
        .context("Failed to parse the partitions config")?;

    let flash_manifest_path = args.outdir.join("flash.json");
    let mut flash_manifest_file = File::create(&flash_manifest_path)
        .context(format!("Failed to create: {}", flash_manifest_path.display()))?;
    flash_manifest_file.write_all(b"{}")?;
    Ok(())
}
