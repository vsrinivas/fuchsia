// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error, Result},
    ffx_scrutiny_verify_args::route_sources::Command,
    scrutiny_config::{ConfigBuilder, ModelConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_plugins::verify::{RouteSourceError, VerifyRouteSourcesResults},
    serde_json,
    std::{
        collections::{HashMap, HashSet},
        convert::TryFrom,
        path::PathBuf,
    },
};

struct Query {
    product_bundle: PathBuf,
    config_path: String,
    tmp_dir_path: Option<PathBuf>,
}

impl Query {
    fn with_temporary_directory(mut self, tmp_dir_path: Option<&PathBuf>) -> Self {
        self.tmp_dir_path = tmp_dir_path.map(PathBuf::clone);
        self
    }
}

impl TryFrom<&Command> for Query {
    type Error = Error;
    fn try_from(cmd: &Command) -> Result<Self, Self::Error> {
        let config_path = cmd.config.to_str().ok_or_else(|| {
            anyhow!(
                "Route sources configuration file path {:?} cannot be converted to string for passing to scrutiny",
                cmd.config
            )
        })?;
        let config_path = config_path.to_string();
        Ok(Query { product_bundle: cmd.product_bundle.clone(), config_path, tmp_dir_path: None })
    }
}

fn verify_route_sources(query: Query) -> Result<HashSet<PathBuf>> {
    let command =
        CommandBuilder::new("verify.route_sources").param("input", query.config_path).build();
    let plugins = vec!["DevmgrConfigPlugin", "StaticPkgsPlugin", "CorePlugin", "VerifyPlugin"];
    let model = ModelConfig::from_product_bundle(&query.product_bundle)?;
    let mut config = ConfigBuilder::with_model(model).command(command).plugins(plugins).build();
    config.runtime.logging.silent_mode = true;
    config.runtime.model.tmp_dir_path = query.tmp_dir_path;

    let scrutiny_output =
        launcher::launch_from_config(config).context("Failed to run verify.route_sources")?;
    let route_sources_results: VerifyRouteSourcesResults = serde_json::from_str(&scrutiny_output)
        .context(format!(
        "Failed to parse verify.route_sources JSON output as structured results: {}",
        scrutiny_output
    ))?;

    let mut errors = HashMap::new();
    for (path, results) in route_sources_results.results.iter() {
        let component_errors: Vec<&RouteSourceError> =
            results.iter().filter_map(|result| result.result.as_ref().err()).collect();
        if component_errors.len() > 0 {
            errors.insert(path.clone(), component_errors);
        }
    }
    if errors.len() > 0 {
        return Err(anyhow!("verify.route_sources reported errors: {:#?}", errors));
    }

    Ok(route_sources_results.deps)
}

pub async fn verify(cmd: &Command, tmp_dir: Option<&PathBuf>) -> Result<HashSet<PathBuf>> {
    let query = Query::try_from(cmd)?.with_temporary_directory(tmp_dir);
    let deps = verify_route_sources(query)?;
    Ok(deps)
}
