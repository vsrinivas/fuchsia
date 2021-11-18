// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::pbm::make_configs;
use anyhow::{Context, Result};
use ffx_core::ffx_plugin;
use ffx_emulator_engines::EngineBuilder;
use ffx_emulator_start_args::StartCommand;
use fidl_fuchsia_developer_bridge as bridge;

mod pbm;

#[ffx_plugin("emu.experimental")]
pub async fn start(cmd: StartCommand, _daemon_proxy: bridge::DaemonProxy) -> Result<()> {
    let emulator_configuration =
        make_configs(&cmd).await.context("making configuration from metadata")?;

    // Initialize an engine of the requested type with the configuration defined in the manifest.
    let mut engine =
        EngineBuilder::new().config(emulator_configuration).engine_type(cmd.engine).build().await?;

    std::process::exit(engine.start().await?)
}
