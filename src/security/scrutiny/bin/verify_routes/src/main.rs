// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Result},
    clap::{App, Arg},
    scrutiny_config::Config,
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    serde::{Deserialize, Serialize},
    serde_json,
    std::{env, fs},
};

pub struct VerifyRoutes {
    stamp_path: String,
    depfile_path: String,
    allowlist_path: String,
}

/// A serde compatible structure for AnalysisEntrys that are used by both
/// allowlist entries and the results from the analysis.
#[derive(Serialize, Deserialize, Debug)]
struct AnalysisEntry {
    capability_type: String,
    results: AnalysisResults,
}

#[derive(Serialize, Deserialize, Default, Debug)]
struct AnalysisResults {
    errors: Vec<AnalysisError>,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
struct AnalysisError {
    capability: String,
    using_node: String,
    error: Option<String>,
}

/// Paths required for depfile generation. Both these locations are touched
/// by the core DataCollector.
const AMBER_PATH: &str = "amber-files/repository";
const AMBER_BLOB_PATH: &str = "amber-files/repository/blobs";
const AMBER_TARGETS_PATH: &str = "amber-files/repository/targets.json";

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
        VerifyRoutes::generate_depfile(&self.stamp_path, &self.depfile_path)?;
        let mut config = Config::run_command_with_plugins(
            CommandBuilder::new("verify.capability_routes")
                .param("capability_types", "directory protocol")
                .param("response_level", "error")
                .build(),
            vec!["CorePlugin", "VerifyPlugin"],
        );
        config.runtime.model.build_path = env::current_dir()?;
        config.runtime.model.repository_path = env::current_dir()?.join(AMBER_PATH);
        config.runtime.logging.silent_mode = true;
        let route_analysis: Vec<AnalysisEntry> =
            serde_json5::from_str(&launcher::launch_from_config(config)?)?;
        let allowlist: Vec<AnalysisEntry> =
            serde_json5::from_str(&fs::read_to_string(&self.allowlist_path)?)?;
        let filtered_analysis = VerifyRoutes::filter_analysis(route_analysis, allowlist);
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

        fs::write(&self.stamp_path, "Verified\n")?;
        Ok(())
    }

    /// Removes entries defined in the file located at the `allowlist_path`
    /// from the route analysis.
    fn filter_analysis(
        route_analysis: Vec<AnalysisEntry>,
        allowlist: Vec<AnalysisEntry>,
    ) -> Vec<AnalysisEntry> {
        let mut filtered_analysis: Vec<AnalysisEntry> = Vec::new();
        for entry in route_analysis.iter() {
            let mut filtered_entry = AnalysisEntry {
                capability_type: entry.capability_type.clone(),
                results: AnalysisResults::default(),
            };
            match allowlist.iter().find(|e| e.capability_type == entry.capability_type) {
                Some(filter) => {
                    for error in entry.results.errors.iter() {
                        if filter
                            .results
                            .errors
                            .iter()
                            .find(|e| {
                                e.capability == error.capability && e.using_node == error.using_node
                            })
                            .is_none()
                        {
                            filtered_entry.results.errors.push(error.clone());
                        }
                    }
                }
                None => {}
            }
            filtered_analysis.push(filtered_entry);
        }
        filtered_analysis
    }

    /// Generates a list of all files that this compiled_action touches. This is
    /// required to be a hermetic action.
    fn generate_depfile(stamp_path: &str, depfile_path: &str) -> Result<()> {
        let mut stamp = String::from(stamp_path);
        stamp.push_str(": ");
        stamp.push_str(AMBER_TARGETS_PATH);
        stamp.push_str(" ");
        let blob_paths = fs::read_dir(AMBER_BLOB_PATH)?;
        for blob_path in blob_paths {
            stamp.push_str(&blob_path.unwrap().path().into_os_string().into_string().unwrap());
            stamp.push_str(" ");
        }
        fs::write(depfile_path, stamp)?;
        Ok(())
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
