// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Result};
use ffx_scrutiny_verify_args::structured_config::Command;
use scrutiny_config::{ConfigBuilder, ModelConfig};
use scrutiny_frontend::{command_builder::CommandBuilder, launcher};
use scrutiny_plugins::verify::VerifyStructuredConfigResponse;
use std::{collections::HashSet, path::PathBuf};

pub async fn verify(cmd: &Command) -> Result<HashSet<PathBuf>> {
    let policy_path = &cmd.policy.to_str().context("converting policy path to string")?.to_owned();
    let command = CommandBuilder::new("verify.structured_config")
        .param("policy", policy_path.clone())
        .build();
    let plugins = vec![
        "CorePlugin".to_string(),
        "DevmgrConfigPlugin".to_string(),
        "StaticPkgsPlugin".to_string(),
        "VerifyPlugin".to_string(),
    ];
    let model = ModelConfig::from_product_bundle(&cmd.product_bundle)?;
    let mut config = ConfigBuilder::with_model(model).command(command).plugins(plugins).build();
    config.runtime.logging.silent_mode = true;

    let scrutiny_output = launcher::launch_from_config(config).context("launching scrutiny")?;

    let response: VerifyStructuredConfigResponse = serde_json::from_str(&scrutiny_output)
        .with_context(|| format!("deserializing verify response `{}`", scrutiny_output))?;

    response.check_errors().with_context(|| {
        format!("checking scrutiny output for verification errors against policy in {policy_path}")
    })?;

    Ok(response.deps)
}
