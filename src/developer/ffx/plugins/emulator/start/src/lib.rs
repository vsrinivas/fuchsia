// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::pbm::update_engine_with_pbm;
use anyhow::Context;
use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_config::EmulatorEngine;
use ffx_emulator_engines_vdl::VdlEngine;
use ffx_emulator_start_args::StartCommand;
use fidl_fuchsia_developer_bridge as bridge;

mod pbm;

#[ffx_plugin("emu.experimental")]
pub async fn start(
    cmd: StartCommand,
    daemon_proxy: bridge::DaemonProxy,
) -> Result<(), anyhow::Error> {
    // TODO(fxbug.dev/88352): This will be a match statement to instantiate the
    // correct engine type based on what's specified on the CLI, but for now
    // there's only the one type so it's hard-coded.
    let mut engine = VdlEngine::new();
    let config = FfxConfigWrapper::new();
    // TODO(fxbug.dev/88355): This updates the engine with values from the
    // manifest. Currently, the manifest doesn't actually hold most of the
    // parameters needed, so we added a non-standard setup_vdl_files to make up
    // the difference for now. This will go away as the manifest matures.
    update_engine_with_pbm(&cmd, &mut engine, &config).await.context("update engine with pbm").ok();
    engine.setup_vdl_files(cmd, daemon_proxy).await.ok();
    std::process::exit(engine.start().unwrap())
}
