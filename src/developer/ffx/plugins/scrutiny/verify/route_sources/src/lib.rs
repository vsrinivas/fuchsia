// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Error, Result},
    ffx_core::ffx_plugin,
    ffx_scrutiny_route_sources_args::ScrutinyRouteSourcesCommand,
    scrutiny_config::{Config, LoggingConfig, ModelConfig, PluginConfig, RuntimeConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_plugins::verify::{RouteSourceError, VerifyRouteSourcesResults},
    serde_json,
    std::{
        collections::HashMap,
        fs::{self, File},
        io::Write,
        path::PathBuf,
    },
};

struct VerifyRouteSources {
    route_sources_query_path: String,
    build_path: String,
    zbi_path: String,
    depfile_path: Option<String>,
    manifest_path: String,
    stamp_path: Option<String>,
}

impl VerifyRouteSources {
    /// The route sources verifier extracts the verifies that a configurable set
    /// of component routes are sourced from an expected component instance
    /// capability. For example, the configuration may specify that a particular
    /// directory capability must come from a specific component instance and be
    /// mapped from a particular directory prefix. This verifier relies on ZBI
    /// devmgr config, static packages index, core package, and component tree
    /// data collectors to verify where component manifests are loaded from as
    /// well as the source capability for verified routes.
    pub fn verify(&self) -> Result<()> {
        let build_path = PathBuf::from(self.build_path.clone());
        let model = ModelConfig {
            zbi_path: self.zbi_path.clone(),
            blob_manifest_path: PathBuf::from(self.manifest_path.clone()),
            build_path: build_path.clone(),
            repository_path: build_path.join("amber-files/repository"),
            ..ModelConfig::minimal()
        };

        let runtime_config = RuntimeConfig::minimal();
        let config = Config::run_command_with_runtime(
            CommandBuilder::new("verify.route_sources")
                .param("input", &self.route_sources_query_path)
                .build(),
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
        let route_sources_results: VerifyRouteSourcesResults =
            serde_json::from_str(&scrutiny_output).context(format!(
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

        // Write out the depfile.
        if let Some(depfile_path) = self.depfile_path.as_ref() {
            let stamp_path = self
                .stamp_path
                .as_ref()
                .ok_or(anyhow!("Cannot specify depfile without specifying stamp"))?;

            let deps: Vec<String> = route_sources_results.deps.into_iter().collect();
            let mut depfile = File::create(depfile_path).context("Failed to create dep file")?;
            write!(depfile, "{}: {}", stamp_path, deps.join(" "))
                .context("Failed to write to dep file")?;
        }

        Ok(())
    }
}
#[ffx_plugin()]
pub async fn scrutiny_route_sources(cmd: ScrutinyRouteSourcesCommand) -> Result<(), Error> {
    if cmd.depfile.is_some() && cmd.stamp.is_none() {
        bail!("Cannot specify --depfile without --stamp");
    }

    let route_sources_query_path = cmd.config;
    let build_path = cmd.build_path;
    let zbi_path = cmd.zbi;
    let depfile_path = cmd.depfile;
    let manifest_path = cmd.blobfs_manifest;
    let stamp_path = cmd.stamp;

    let verifier = VerifyRouteSources {
        route_sources_query_path,
        build_path,
        zbi_path,
        depfile_path,
        manifest_path,
        stamp_path: stamp_path.clone(),
    };
    verifier.verify()?;

    if let Some(stamp_path) = stamp_path {
        fs::write(stamp_path, "Verified").context("Failed to write stamp file")?;
    }

    Ok(())
}
