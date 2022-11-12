// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    ffx_core::ffx_plugin,
    ffx_scrutiny_zbi_args::ScrutinyZbiCommand,
    scrutiny_config::{Config, LaunchConfig, RuntimeConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
};

#[ffx_plugin()]
pub async fn scrutiny_zbi(cmd: ScrutinyZbiCommand) -> Result<(), Error> {
    let config = Config {
        launch: LaunchConfig {
            command: Some(
                CommandBuilder::new("tool.zbi.extract")
                    .param("input", cmd.input)
                    .param("output", cmd.output)
                    .build(),
            ),
            script_path: None,
        },
        runtime: RuntimeConfig::minimal(),
    };
    launcher::launch_from_config(config)?;

    Ok(())
}
