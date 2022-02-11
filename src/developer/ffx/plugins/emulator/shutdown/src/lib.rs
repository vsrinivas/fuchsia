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
use fidl_fuchsia_developer_bridge::TargetCollectionProxy;
use std::path::PathBuf;

async fn attempt_shutdown(instance_dir: &PathBuf, proxy: &TargetCollectionProxy) -> Result<()> {
    if !instance_dir.exists() {
        // If the directory doesn't exist, we just return Ok(()) since there's nothing to shut down
        return Ok(());
    }
    let engine = read_from_disk(instance_dir).context(
        "Couldn't deserialize engine from disk. \
        Continuing shutdown, but you may need to terminate the emulator process manually.",
    );
    match engine {
        Ok(engine) => engine.shutdown(proxy).await,
        Err(e) => Err(e),
    }
}

#[ffx_plugin(TargetCollectionProxy = "daemon::protocol")]
pub async fn shutdown(cmd: ShutdownCommand, proxy: TargetCollectionProxy) -> Result<()> {
    let ffx_config = FfxConfigWrapper::new();
    let instances: Vec<PathBuf> = if cmd.all {
        get_all_instances(&ffx_config).await?
    } else {
        vec![get_instance_dir(&ffx_config, &cmd.name, false).await?]
    };
    for path in instances {
        let result = attempt_shutdown(&path, &proxy).await;
        if result.is_err() {
            println!("{:?}", result.unwrap_err());
        }
        if !cmd.persist {
            let cleanup = clean_up_instance_dir(&path).await;
            if cleanup.is_err() {
                println!("{:?}", cleanup.unwrap_err());
            }
        }
    }
    Ok(())
}
