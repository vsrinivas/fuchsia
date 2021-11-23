// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_engines::{
    clean_up_instance_dir, get_all_instances, get_instance_dir, serialization::read_from_disk,
};
use ffx_emulator_shutdown_args::ShutdownCommand;

async fn attempt_shutdown(name: &str, ffx_config: &FfxConfigWrapper) -> Result<()> {
    let instance_dir = get_instance_dir(&ffx_config, name, false)
        .await
        .with_context(|| format!("Couldn't locate instance directory for {}.", name))?;
    if !instance_dir.exists() {
        // If the directory doesn't exist, we just return Ok(()) since there's nothing to shut down
        return Ok(());
    }
    let mut engine = read_from_disk(&instance_dir).await?;
    engine.shutdown()
}

#[ffx_plugin("emu.experimental")]
pub async fn shutdown(cmd: ShutdownCommand) -> Result<()> {
    let ffx_config = FfxConfigWrapper::new();
    let instances: Vec<String> = if cmd.all { get_all_instances() } else { vec![cmd.name] };
    for name in instances {
        let result = attempt_shutdown(&name, &ffx_config).await;
        if result.is_err() {
            println!("{:?}", result.unwrap_err());
        }
        let cleanup = clean_up_instance_dir(&ffx_config, &name).await;
        if cleanup.is_err() {
            println!("{:?}", cleanup.unwrap_err());
        }
    }
    Ok(())
}
