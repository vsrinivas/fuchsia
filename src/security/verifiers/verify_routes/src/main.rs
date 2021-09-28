// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Context, Result},
    clap::{App, Arg},
    scrutiny_config::Config,
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_plugins::verify::{
        CapabilityRouteResults, ResultsBySeverity, ResultsForCapabilityType,
    },
    serde_json,
    std::{env, fs, io::Write},
};

pub struct VerifyRoutes {
    stamp_path: String,
    depfile_path: String,
    allowlist_path: String,
}

/// Paths required for depfile generation. Both these locations are touched
/// by the core DataCollector.
const AMBER_PATH: &str = "amber-files/repository";

impl VerifyRoutes {
    /// Creates a new VerifyRoute instance with a `stamp_path` that is written
    /// to if the verification succeeds. A `depfile_path` that lists all the
    /// files this executable touches and a `allowlist_path` which lists all
    /// the moniker & protocol pairs that are filtered from the analysis.
    fn new<S: Into<String>>(stamp_path: S, depfile_path: S, allowlist_path: S) -> Self {
        Self {
            stamp_path: stamp_path.into(),
            depfile_path: depfile_path.into(),
            allowlist_path: allowlist_path.into(),
        }
    }

    /// Launches Scrutiny and performs the capability route analysis. The
    /// results are then filtered based on the provided allowlist and any
    /// errors that are not allowlisted cause the verification to fail listing
    /// all non-allowlisted errors.
    fn verify(&self) -> Result<()> {
        let mut config = Config::run_command_with_plugins(
            CommandBuilder::new("verify.capability_routes")
                .param("capability_types", "directory protocol")
                .param("response_level", "error")
                .build(),
            vec!["DevmgrConfigPlugin", "StaticPkgsPlugin", "CorePlugin", "VerifyPlugin"],
        );
        let current_dir = env::current_dir().context("Failed to get current directory")?;
        config.runtime.model.build_path = current_dir.clone();
        config.runtime.model.repository_path = current_dir.join(AMBER_PATH);
        config.runtime.logging.silent_mode = true;

        let results = launcher::launch_from_config(config).context("Failed to launch scrutiny")?;
        let route_analysis: CapabilityRouteResults = serde_json5::from_str(&results)
            .context(format!("Failed to deserialize verify routes results: {}", results))?;
        let allowlist: Vec<ResultsForCapabilityType> = serde_json5::from_str(
            &fs::read_to_string(&self.allowlist_path).context("Failed to read allowlist")?,
        )
        .context("Failed to deserialize allowlist")?;
        let filtered_analysis = VerifyRoutes::filter_analysis(route_analysis.results, allowlist);
        for entry in filtered_analysis.iter() {
            if !entry.results.errors.is_empty() {
                bail!(
                    "
Static Capability Flow Analysis Error:
The route verifier failed to verify all capability routes in this build.

See https://fuchsia.dev/fuchsia-src/development/components/v2/troubleshooting#static-analyzer

If the broken route is required for a transition it can be temporarily added
to the allowlist located at: {}

Verification Errors:
{}",
                    self.allowlist_path,
                    serde_json::to_string_pretty(&filtered_analysis).unwrap()
                );
            }
        }

        // Write out the depfile.
        let deps: Vec<String> = route_analysis.deps.into_iter().collect();
        let mut depfile =
            fs::File::create(&self.depfile_path).context("Failed to create dep file")?;
        write!(depfile, "{}: {}", self.stamp_path, deps.join(" "))
            .context("Failed to write to dep file")?;

        fs::write(&self.stamp_path, "Verified\n").context("failed to write stamp file")?;
        Ok(())
    }

    /// Removes entries defined in the file located at the `allowlist_path`
    /// from the route analysis.
    fn filter_analysis(
        route_analysis: Vec<ResultsForCapabilityType>,
        allowlist: Vec<ResultsForCapabilityType>,
    ) -> Vec<ResultsForCapabilityType> {
        route_analysis
            .iter()
            .map(|analysis_item| {
                // Entry for every `capability_type` in `route_analysis`.
                ResultsForCapabilityType {
                    capability_type: analysis_item.capability_type.clone(),
                    // Retain error when:
                    // 1. `allowlist` does not have results for
                    //    `capability_type` (i.e., nothing allowed for
                    //    `capability_type`), OR
                    // 2. `allowlist` does not have an identical `allow_error`
                    //    in its `capability_type` results.
                    results: ResultsBySeverity {
                        errors: analysis_item
                            .results
                            .errors
                            .iter()
                            .filter_map(|analysis_error| {
                                match allowlist.iter().find(|&allow_item| {
                                    allow_item.capability_type == analysis_item.capability_type
                                }) {
                                    Some(allow_item) => {
                                        match allow_item
                                            .results
                                            .errors
                                            .iter()
                                            .find(|&allow_error| analysis_error == allow_error)
                                        {
                                            Some(_matching_allowlist_error) => None,
                                            // No allowlist error match; report
                                            // error from within `filter_map`.
                                            None => Some(analysis_error.clone()),
                                        }
                                    }
                                    // No allowlist defined for capability type;
                                    // report error from within `filter_map`.
                                    None => Some(analysis_error.clone()),
                                }
                            })
                            .collect(),
                        ..Default::default()
                    },
                }
            })
            .collect()
    }
}

/// A small shim interface around the Scrutiny framework that takes the
/// `stamp`, `depfile` and `allowlist` paths from the build.
fn main() -> Result<()> {
    simplelog::SimpleLogger::init(simplelog::LevelFilter::Error, simplelog::Config::default())?;
    let args = App::new("scrutiny_verify_routes")
        .version("1.0")
        .author("Fuchsia Authors")
        .about("Verifies component framework v2 capability routes")
        .arg(
            Arg::with_name("stamp")
                .long("stamp")
                .required(true)
                .help("The stamp file output location")
                .value_name("stamp")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("depfile")
                .long("depfile")
                .required(true)
                .help("The dep file output location")
                .value_name("depfile")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("allowlist")
                .long("allowlist")
                .required(true)
                .help("The allowlist file input location")
                .value_name("allowlist")
                .takes_value(true),
        )
        .get_matches();

    let verify_routes = VerifyRoutes::new(
        args.value_of("stamp").unwrap(),
        args.value_of("depfile").unwrap(),
        args.value_of("allowlist").unwrap(),
    );
    verify_routes.verify()
}
