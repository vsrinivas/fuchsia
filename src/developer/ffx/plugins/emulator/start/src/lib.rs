// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::pbm::make_configs;
use anyhow::Context;
use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_config::EmulatorEngine;
use ffx_emulator_engines::{FemuEngine, QemuEngine};
use ffx_emulator_start_args::{EngineType, StartCommand};
use fidl_fuchsia_developer_bridge as bridge;

mod pbm;

#[ffx_plugin("emu.experimental")]
pub async fn start(
    cmd: StartCommand,
    _daemon_proxy: bridge::DaemonProxy,
) -> Result<(), anyhow::Error> {
    let config = FfxConfigWrapper::new();

    let mut engine: Box<dyn EmulatorEngine> = match cmd.engine {
        EngineType::Femu => Box::new(FemuEngine::new()),
        EngineType::Qemu => Box::new(QemuEngine::new()),
    };

    let emulator_configuration =
        make_configs(&cmd).await.context("making configuration from metadata")?;

    // Allow the engine to initialize and validate the configuration. The configuration is mutable
    // to all the engine to potentially modify the configuration based on how the configuration
    // is interpreted by the engine.
    engine
        .initialize(&config, emulator_configuration)
        .await
        .context("initializing emulator engine")?;

    std::process::exit(engine.start().await?)
}
