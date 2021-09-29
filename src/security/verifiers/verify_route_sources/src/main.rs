// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    clap::{App, Arg},
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
    depfile_path: String,
    manifest_path: String,
    stamp_path: String,
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
        let deps: Vec<String> = route_sources_results.deps.into_iter().collect();
        let mut depfile = File::create(&self.depfile_path).context("Failed to create dep file")?;
        write!(depfile, "{}: {}", self.stamp_path, deps.join(" "))
            .context("Failed to write to dep file")?;

        Ok(())
    }
}

fn main() -> Result<()> {
    simplelog::SimpleLogger::init(simplelog::LevelFilter::Error, simplelog::Config::default())?;
    let args = App::new("scrutiny_route_sources")
        .version("1.0")
        .author("Fuchsia Authors")
        .about("Check the selected component's route sources.")
        .arg(
            Arg::with_name("stamp")
                .long("stamp")
                .required(true)
                .help("The stamp file output location.")
                .value_name("stamp")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("depfile")
                .long("depfile")
                .required(true)
                .help("The depfile output location.")
                .value_name("depfile")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("manifest")
                .long("manifest")
                .required(true)
                .help("The blobfs manifest input location.")
                .value_name("manifest")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("config")
                .long("config")
                .required(true)
                .help("Path to configuration file that defines component routes to check.")
                .value_name("config")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("zbi")
                .long("zbi")
                .required(true)
                .help("Path to the ZBI to verify.")
                .value_name("zbi")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("build-path")
                .long("build-path")
                .required(true)
                .help("Root path to build artifacts.")
                .value_name("build-path")
                .takes_value(true),
        )
        .get_matches();
    let route_sources_query_path =
        args.value_of("config").expect("failed to find the config path").to_string();
    let build_path =
        args.value_of("build-path").expect("failed to find the build path").to_string();
    let zbi_path = args.value_of("zbi").expect("failed to find the zbi path").to_string();
    let depfile_path =
        args.value_of("depfile").expect("failed to find the depfile path").to_string();
    let manifest_path =
        args.value_of("manifest").expect("failed to find the blobfs manifest path").to_string();
    let stamp_path = args.value_of("stamp").expect("failed to find the stamp path").to_string();

    let verifier = VerifyRouteSources {
        route_sources_query_path,
        build_path,
        zbi_path,
        depfile_path,
        manifest_path,
        stamp_path: stamp_path.clone(),
    };
    match verifier.verify() {
        Ok(()) => {
            fs::write(stamp_path, "Verified").expect("Failed to write stamp file");
            Ok(())
        }
        error => error,
    }
}
