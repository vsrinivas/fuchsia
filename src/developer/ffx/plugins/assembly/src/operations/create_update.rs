// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use assembly_manifest::AssemblyManifest;
use assembly_partitions_config::PartitionsConfig;
use assembly_tool::SdkToolProvider;
use assembly_update_package::{Slot, UpdatePackageBuilder};
use assembly_update_packages_manifest::UpdatePackagesManifest;
use assembly_util::from_reader;
use epoch::EpochFile;
use ffx_assembly_args::CreateUpdateArgs;
use std::fs::File;
use std::path::Path;

pub fn create_update(args: CreateUpdateArgs) -> Result<()> {
    // Use the sdk to get the host tool paths.
    let sdk_tools = SdkToolProvider::try_new().context("Getting SDK tools")?;

    let mut file = File::open(&args.partitions)
        .context(format!("Failed to open: {}", args.partitions.display()))?;
    let partitions = PartitionsConfig::from_reader(&mut file)
        .context("Failed to parse the partitions config")?;
    let epoch: EpochFile = EpochFile::Version1 { epoch: args.epoch };
    let mut builder = UpdatePackageBuilder::new(
        Box::new(sdk_tools),
        partitions,
        args.board_name,
        args.version_file,
        epoch,
        /*abi_revision=*/ None,
        &args.outdir,
    );

    // Set the package name.
    // Typically used for OTA tests.
    if let Some(name) = args.update_package_name {
        builder.set_name(name);
    }

    // Add the packages to update.
    if let Some(packages_path) = &args.packages {
        let mut file = File::open(packages_path)
            .context(format!("Failed to open: {}", packages_path.display()))?;
        let packages: UpdatePackagesManifest =
            from_reader(&mut file).context("Failed to parse the packages manifest")?;
        builder.add_packages(packages);
    }

    // Set the gendir separate from the outdir.
    if let Some(gendir) = args.gendir {
        builder.set_gendir(gendir);
    }

    // Set the images to update in the primary slot.
    if let Some(manifest) = args.system_a.as_ref().map(manifest_from_file).transpose()? {
        builder.add_slot_images(Slot::Primary(manifest));
    }

    // Set the images to update in the recovery slot.
    if let Some(manifest) = args.system_r.as_ref().map(manifest_from_file).transpose()? {
        builder.add_slot_images(Slot::Recovery(manifest));
    }

    builder.build()?;
    Ok(())
}

fn manifest_from_file(path: impl AsRef<Path>) -> Result<AssemblyManifest> {
    let file = File::open(path.as_ref())
        .context(format!("Failed to open the system images file: {}", path.as_ref().display()))?;
    serde_json::from_reader(file)
        .context(format!("Failed to parse the system images file: {}", path.as_ref().display()))
}
