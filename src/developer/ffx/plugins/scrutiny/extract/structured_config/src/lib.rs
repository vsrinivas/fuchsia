// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use ffx_core::ffx_plugin;
use ffx_scrutiny_structured_config_args::ScrutinyStructuredConfigCommand;
use scrutiny_config::{Config, LoggingConfig, ModelConfig, PluginConfig, RuntimeConfig};
use scrutiny_frontend::{command_builder::CommandBuilder, launcher};
use scrutiny_plugins::verify::ExtractStructuredConfigResponse;
use scrutiny_utils::path::{join_and_canonicalize, relativize_path};

#[ffx_plugin()]
pub async fn scrutiny_structured_config(cmd: ScrutinyStructuredConfigCommand) -> Result<(), Error> {
    let ScrutinyStructuredConfigCommand { output, build_path, update, blobfs, depfile } = cmd;

    let config = Config::run_command_with_runtime(
        CommandBuilder::new("verify.structured_config.extract").build(),
        RuntimeConfig {
            model: ModelConfig {
                update_package_path: join_and_canonicalize(&build_path, &update),
                blobfs_paths: blobfs
                    .iter()
                    .map(|blobfs| join_and_canonicalize(&build_path, blobfs))
                    .collect(),
                build_path: build_path.clone(),
                ..ModelConfig::minimal()
            },
            logging: LoggingConfig { silent_mode: true, ..LoggingConfig::minimal() },
            // We need to list only the plugins we need or we end up with leaks from Arc cycles.
            // TODO(https://fxbug.dev/106727) resolve Arc leak
            plugin: PluginConfig {
                plugins: vec![
                    "CorePlugin".into(),
                    "DevmgrConfigPlugin".into(),
                    "StaticPkgsPlugin".into(),
                    "VerifyPlugin".into(),
                ],
            },
            ..RuntimeConfig::minimal()
        },
    );
    let scrutiny_output = launcher::launch_from_config(config).context("running scrutiny")?;

    let ExtractStructuredConfigResponse { components, deps } =
        serde_json::from_str(&scrutiny_output)
            .with_context(|| format!("deserializing verify response `{}`", scrutiny_output))?;

    let result = serde_json::to_string_pretty(&components).context("prettifying response JSON")?;
    std::fs::write(&output, result).context("writing output to file")?;

    let relative_dep_paths = deps
        .iter()
        .map(|dep_path| relativize_path(&build_path, dep_path).display().to_string())
        .collect::<Vec<_>>();
    let depfile_contents = format!("{}: {}", output.display(), relative_dep_paths.join(" "));
    std::fs::write(&depfile, depfile_contents).context("writing depfile")?;

    Ok(())
}
