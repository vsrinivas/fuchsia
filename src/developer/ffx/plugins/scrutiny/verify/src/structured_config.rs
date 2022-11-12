// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Result};
use ffx_scrutiny_verify_args::structured_config::Command;
use scrutiny_config::{Config, LoggingConfig, ModelConfig, PluginConfig, RuntimeConfig};
use scrutiny_frontend::{command_builder::CommandBuilder, launcher};
use scrutiny_plugins::verify::VerifyStructuredConfigResponse;
use scrutiny_utils::path::join_and_canonicalize;
use std::{collections::HashSet, path::PathBuf};

pub async fn verify(cmd: &Command, tmp_dir: Option<&PathBuf>) -> Result<HashSet<PathBuf>> {
    let policy_path = join_and_canonicalize(&cmd.build_path, &cmd.policy)
        .to_str()
        .context("converting policy path to string")?
        .to_owned();
    let update_package_path = join_and_canonicalize(&cmd.build_path, &cmd.update);
    let blobfs_paths =
        cmd.blobfs.iter().map(|blobfs| join_and_canonicalize(&cmd.build_path, blobfs)).collect();
    let config = Config::run_command_with_runtime(
        CommandBuilder::new("verify.structured_config")
            .param("policy", policy_path.clone())
            .build(),
        RuntimeConfig {
            model: ModelConfig {
                update_package_path: update_package_path,
                blobfs_paths: blobfs_paths,
                build_path: cmd.build_path.clone(),
                tmp_dir_path: tmp_dir.cloned(),
                ..ModelConfig::minimal()
            },
            logging: LoggingConfig { silent_mode: true, ..LoggingConfig::minimal() },
            // We need to list only the plugins we need or we end up with leaks from Arc cycles.
            // TODO(https://fxbug.dev/106727) use the default PluginConfig
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
    let scrutiny_output = launcher::launch_from_config(config).context("launching scrutiny")?;

    let response: VerifyStructuredConfigResponse = serde_json::from_str(&scrutiny_output)
        .with_context(|| format!("deserializing verify response `{}`", scrutiny_output))?;

    response.check_errors().with_context(|| {
        format!("checking scrutiny output for verification errors against policy in {policy_path}")
    })?;

    Ok(response.deps)
}
