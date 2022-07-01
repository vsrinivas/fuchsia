// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error, Result},
    ffx_scrutiny_verify_args::route_sources::Command,
    scrutiny_config::{Config, LoggingConfig, ModelConfig, PluginConfig, RuntimeConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_plugins::verify::{RouteSourceError, VerifyRouteSourcesResults},
    scrutiny_utils::path::join_and_canonicalize,
    serde_json,
    std::{
        collections::{HashMap, HashSet},
        convert::{TryFrom, TryInto},
        path::PathBuf,
    },
};

struct Query {
    build_path: PathBuf,
    update_package_path: PathBuf,
    blobfs_paths: Vec<PathBuf>,
    config_path: String,
}

impl TryFrom<&Command> for Query {
    type Error = Error;
    fn try_from(cmd: &Command) -> Result<Self, Self::Error> {
        let config_path_buf = join_and_canonicalize(&cmd.build_path, &cmd.config);
        let config_path = config_path_buf.to_str().ok_or_else(|| {
            anyhow!(
                "Route sources configuration file path {:?} cannot be converted to string for passing to scrutiny",
                cmd.config
            )
        })?;
        let update_package_path = join_and_canonicalize(&cmd.build_path, &cmd.update);
        let blobfs_paths = cmd
            .blobfs
            .iter()
            .map(|blobfs| join_and_canonicalize(&cmd.build_path, blobfs))
            .collect();
        let config_path = config_path.to_string();
        Ok(Query {
            build_path: cmd.build_path.clone(),
            update_package_path,
            blobfs_paths,
            config_path,
        })
    }
}

fn verify_route_sources(query: Query) -> Result<HashSet<PathBuf>> {
    let model = ModelConfig {
        update_package_path: query.update_package_path,
        blobfs_paths: query.blobfs_paths,
        build_path: query.build_path,
        ..ModelConfig::minimal()
    };

    let runtime_config = RuntimeConfig::minimal();
    let config = Config::run_command_with_runtime(
        CommandBuilder::new("verify.route_sources").param("input", query.config_path).build(),
        RuntimeConfig {
            model,
            logging: LoggingConfig { silent_mode: true, ..LoggingConfig::minimal() },
            plugin: PluginConfig {
                plugins: vec![
                    "DevmgrConfigPlugin",
                    "StaticPkgsPlugin",
                    "CorePlugin",
                    "VerifyPlugin",
                ]
                .into_iter()
                .map(String::from)
                .collect(),
            },
            ..runtime_config
        },
    );

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

pub async fn verify(cmd: &Command) -> Result<HashSet<PathBuf>> {
    let query = cmd.try_into()?;
    let deps = verify_route_sources(query)?;
    Ok(deps)
}
