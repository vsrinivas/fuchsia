// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::editor::edit_configuration;
use crate::pbm::{list_virtual_devices, make_configs};
use anyhow::Result;
use errors::ffx_bail;
use ffx_core::ffx_plugin;
use ffx_emulator_config::EngineType;
use ffx_emulator_engines::EngineBuilder;
use ffx_emulator_start_args::StartCommand;
use fidl_fuchsia_developer_ffx::TargetCollectionProxy;
use std::str::FromStr;

mod editor;
mod pbm;

#[ffx_plugin(TargetCollectionProxy = "daemon::protocol")]
pub async fn start(cmd: StartCommand, proxy: TargetCollectionProxy) -> Result<()> {
    // If device name is list, list the available virtual devices and return.
    if Some(String::from("list")) == cmd.device {
        match list_virtual_devices(&cmd).await {
            Ok(devices) => {
                println!("Valid virtual device specifications are: {:?}", devices);
                return Ok(());
            }
            Err(e) => {
                ffx_bail!("{:?}", e.context("Listing available virtual device specifications"))
            }
        };
    }

    let emulator_configuration = match make_configs(&cmd).await {
        Ok(config) => config,
        Err(e) => {
            ffx_bail!("{:?}", e);
        }
    };

    // Initialize an engine of the requested type with the configuration defined in the manifest.
    let engine_type = match EngineType::from_str(&cmd.engine().await.unwrap_or("femu".to_string()))
    {
        Ok(e) => e,
        Err(e) => ffx_bail!("{:?}", e.context("Couldn't retrieve engine type from ffx config.")),
    };
    let mut engine = match EngineBuilder::new()
        .config(emulator_configuration)
        .engine_type(engine_type)
        .build()
        .await
    {
        Ok(engine) => engine,
        Err(e) => ffx_bail!("{:?}", e.context("The emulator could not be configured.")),
    };

    if let Err(e) = engine.stage().await {
        ffx_bail!("{:?}", e.context("Problem staging to the emulator's instance directory."));
    }

    if cmd.edit {
        if let Err(e) = edit_configuration(engine.emu_config_mut()) {
            ffx_bail!("{:?}", e.context("Problem editing configuration."));
        }
    }

    let emulator_cmd = engine.build_emulator_cmd();

    if cmd.verbose || cmd.dry_run {
        println!("[emulator] Running emulator cmd: {:?}\n", emulator_cmd);
        println!("[emulator] Running with ENV: {:?}\n", emulator_cmd.get_envs());
        if cmd.dry_run {
            return Ok(());
        }
    }

    match engine.start(emulator_cmd, &proxy).await {
        Ok(0) => Ok(()),
        Ok(_) => ffx_bail!("Non zero return code"),
        Err(e) => ffx_bail!("{:?}", e.context("The emulator failed to start.")),
    }
}
