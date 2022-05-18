// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use assembly_partitions_config::PartitionsConfig;
use ffx_assembly_args::CreateFlashManifestArgs;
use ffx_fastboot::manifest::{v3, FlashManifestVersion};
use std::fs::File;

pub fn create_flash_manifest(args: CreateFlashManifestArgs) -> Result<()> {
    let mut file = File::open(&args.partitions)
        .context(format!("Failed to open: {}", args.partitions.display()))?;
    let _partitions = PartitionsConfig::from_reader(&mut file)
        .context("Failed to parse the partitions config")?;

    let manifest = FlashManifestVersion::V3(v3::FlashManifest {
        hw_revision: "board".into(),
        ..Default::default()
    });
    let flash_manifest_path = args.outdir.join("flash.json");
    let mut flash_manifest_file = File::create(&flash_manifest_path)
        .context(format!("Failed to create: {}", flash_manifest_path.display()))?;
    manifest.write(&mut flash_manifest_file)?;

    Ok(())
}
