// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::pbm::make_configs;
use anyhow::Result;
use errors::ffx_bail;
use ffx_config::sdk::SdkVersion;
use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_config::EngineType;
use ffx_emulator_engines::EngineBuilder;
use ffx_emulator_start_args::StartCommand;
use fidl_fuchsia_developer_ffx::TargetCollectionProxy;
use std::str::FromStr;

mod pbm;

const OOT_BUNDLE_ERROR: &'static str =
    "Encountered a problem reading the emulator configuration. This may mean you\n\
don't have an appropriate Product Bundle available. Try `ffx product-bundle`\n\
to list and download available bundles.";

const IT_CONFIG_ERROR: &'static str =
    "Encountered a problem reading the emulator configuration. This may mean the\n\
currently selected board configuration isn't supported for emulation. Try\n\
using 'qemu-x64' or 'qemu-arm64' in your `fx set <PRODUCT>.<BOARD>` command\n\
then rebuild to enable emulation.";

#[ffx_plugin(TargetCollectionProxy = "daemon::protocol")]
pub async fn start(cmd: StartCommand, proxy: TargetCollectionProxy) -> Result<()> {
    let config = FfxConfigWrapper::new();
    let in_tree = match ffx_config::get_sdk().await {
        Ok(sdk) => match sdk.get_version() {
            SdkVersion::InTree => true,
            _ => false,
        },
        Err(e) => {
            ffx_bail!("{:?}", e.context("Couldn't find version information from ffx config."))
        }
    };

    let emulator_configuration = match make_configs(&cmd, &config).await {
        Ok(config) => config,
        Err(e) => {
            ffx_bail!("{:?}", e.context(if in_tree { IT_CONFIG_ERROR } else { OOT_BUNDLE_ERROR }));
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

    let emulator_cmd = engine.build_emulator_cmd();

    if cmd.verbose || cmd.dry_run {
        println!("[emulator] Running emulator cmd: {:?}\n", emulator_cmd);
        println!("[emulator] Running with ENV: {:?}\n", emulator_cmd.get_envs());
        if cmd.dry_run {
            return Ok(());
        }
    }

    match engine.start(emulator_cmd, &proxy).await {
        Ok(result) => std::process::exit(result),
        Err(e) => ffx_bail!("{:?}", e.context("The emulator failed to start.")),
    }
}
