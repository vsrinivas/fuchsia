// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    ffx_core::ffx_plugin,
    ffx_scrutiny_shell_args::ScrutinyShellCommand,
    scrutiny_config::{Config, LaunchConfig, LoggingVerbosity, RuntimeConfig},
    scrutiny_frontend::launcher,
    std::path::PathBuf,
};

#[ffx_plugin()]
pub async fn scrutiny_shell(cmd: ScrutinyShellCommand) -> Result<(), Error> {
    let batch_mode = cmd.command.is_some() || cmd.script.is_some();

    let mut config = match batch_mode {
        true => Config {
            launch: LaunchConfig { command: cmd.command, script_path: cmd.script },
            runtime: RuntimeConfig::minimal(),
        },
        false => Config::default(),
    };

    if let Some(build_path) = cmd.build {
        config.runtime.model.build_path = PathBuf::from(build_path);
    }

    if let Some(model_path) = cmd.model {
        config.runtime.model.uri = model_path;
    }

    if let Some(log_path) = cmd.log {
        config.runtime.logging.path = log_path;
    }

    if let Some(verbosity) = cmd.verbosity {
        config.runtime.logging.verbosity = LoggingVerbosity::from(verbosity);
    }

    launcher::launch_from_config(config)?;

    Ok(())
}
