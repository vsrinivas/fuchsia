// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    ffx_core::ffx_plugin,
    ffx_scrutiny_package_list_args::ScrutinyPackageCommand,
    scrutiny_config::{Config, LaunchConfig, RuntimeConfig, ModelConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
};

#[ffx_plugin()]
pub async fn scrutiny_package(cmd: ScrutinyPackageCommand) -> Result<(), Error> {
    let url_string = format!("{}", cmd.url);
    let config = Config {
        launch: LaunchConfig {
            command: Some(
                CommandBuilder::new("search.package.list").param("url", url_string).build(),
            ),
            script_path: None,
        },
        runtime: RuntimeConfig {
            model: ModelConfig::at_path(cmd.build_path),
            ..RuntimeConfig::minimal()
        }
    };
    launcher::launch_from_config(config)?;

    Ok(())
}
