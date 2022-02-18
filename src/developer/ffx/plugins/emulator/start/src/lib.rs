// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::pbm::make_configs;
use anyhow::{Context, Result};
use errors::ffx_bail;
use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_engines::EngineBuilder;
use ffx_emulator_start_args::StartCommand;
use fidl_fuchsia_developer_bridge::TargetCollectionProxy;

mod pbm;

#[ffx_plugin("emu.experimental", TargetCollectionProxy = "daemon::protocol")]
pub async fn start(cmd: StartCommand, proxy: TargetCollectionProxy) -> Result<()> {
    let config = FfxConfigWrapper::new();
    let emulator_configuration =
        match make_configs(&cmd, &config).await.context("making configuration from metadata") {
            Ok(config) => config,
            Err(e) => {
                ffx_bail!("{:?}",
                e.context(
                    "Encountered a problem reading the emulator configuration. This may mean you\n\
                    don't have an appropriate Product Bundle available. Try `ffx product-bundle`\n\
                    to list and download available bundles."
                ));
            }
        };

    // Initialize an engine of the requested type with the configuration defined in the manifest.
    let result =
        EngineBuilder::new().config(emulator_configuration).engine_type(cmd.engine).build().await;

    std::process::exit(match result {
        Ok(mut engine) => engine.start(&proxy).await?,
        Err(e) => {
            ffx_bail!("{:?}", e);
        }
    });
}
