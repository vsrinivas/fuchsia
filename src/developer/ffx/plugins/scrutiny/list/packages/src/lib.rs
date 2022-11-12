// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_scrutiny_packages_list_args::ScrutinyPackagesCommand,
    scrutiny_config::{Config, LaunchConfig, RuntimeConfig, ModelConfig},
    scrutiny_frontend::launcher,
};

#[ffx_plugin()]
pub async fn scrutiny_package(cmd: ScrutinyPackagesCommand) -> Result<()> {
    let config = Config {
        launch: LaunchConfig { command: Some("packages.urls".to_string()), script_path: None },
        runtime: RuntimeConfig {
            model: ModelConfig::at_path(cmd.build_path),
            ..RuntimeConfig::minimal()
        }
    };
    launcher::launch_from_config(config)?;

    Ok(())
}
