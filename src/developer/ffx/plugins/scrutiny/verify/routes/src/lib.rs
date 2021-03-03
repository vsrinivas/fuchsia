// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    ffx_core::ffx_plugin,
    ffx_scrutiny_routes_args::ScrutinyRoutesCommand,
    scrutiny_frontend::{
        config::{Config, LaunchConfig, RuntimeConfig},
        launcher,
    },
};

#[ffx_plugin()]
pub async fn scrutiny_routes(_cmd: ScrutinyRoutesCommand) -> Result<(), Error> {
    let config = Config {
        launch: LaunchConfig {
            command: Some("verify.capability_routes --capability_types protocol directory --response_level error".to_string()),
            script_path: None,
        },
        runtime: RuntimeConfig::minimal(),
    };
    launcher::launch_from_config(config)
}
