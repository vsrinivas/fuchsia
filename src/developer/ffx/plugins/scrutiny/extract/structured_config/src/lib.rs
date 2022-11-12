// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use ffx_core::ffx_plugin;
use ffx_scrutiny_structured_config_args::ScrutinyStructuredConfigCommand;
use scrutiny_config::{ConfigBuilder, ModelConfig};
use scrutiny_frontend::{command_builder::CommandBuilder, launcher};
use scrutiny_plugins::verify::ExtractStructuredConfigResponse;
use scrutiny_utils::path::relativize_path;

#[ffx_plugin()]
pub async fn scrutiny_structured_config(cmd: ScrutinyStructuredConfigCommand) -> Result<(), Error> {
    let ScrutinyStructuredConfigCommand { product_bundle, build_path, depfile, output } = cmd;

    let model = ModelConfig::from_product_bundle(product_bundle)?;
    let command = CommandBuilder::new("verify.structured_config.extract").build();
    let plugins = vec![
        "CorePlugin".to_string(),
        "DevmgrConfigPlugin".to_string(),
        "StaticPkgsPlugin".to_string(),
        "VerifyPlugin".to_string(),
    ];
    let mut config = ConfigBuilder::with_model(model).command(command).plugins(plugins).build();
    config.runtime.logging.silent_mode = true;
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
