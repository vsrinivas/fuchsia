// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Error, Result},
    ffx_core::ffx_plugin,
    ffx_scrutiny_routes_args::{default_capability_types, ScrutinyRoutesCommand},
    scrutiny_config::Config,
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_plugins::verify::{
        CapabilityRouteResults, ResultsBySeverity, ResultsForCapabilityType,
    },
    serde_json,
    std::{fs, io::Write},
};

pub struct VerifyRoutes {
    build_path: String,
    repository_path: String,
    capability_types: Vec<String>,
    response_level: String,
    stamp_path: Option<String>,
    depfile_path: Option<String>,
    allowlist_paths: Vec<String>,
}

fn merge_allowlists(
    allowlist: &mut Vec<ResultsForCapabilityType>,
    fragment: Vec<ResultsForCapabilityType>,
) -> Result<()> {
    for mut fragment_type_group in fragment {
        let mut merged = false;
        for type_group in allowlist.iter_mut() {
            if type_group.capability_type == fragment_type_group.capability_type {
                merged = true;
                type_group.results.errors.append(&mut fragment_type_group.results.errors);
                type_group.results.warnings.append(&mut fragment_type_group.results.warnings);
                type_group.results.ok.append(&mut fragment_type_group.results.ok);
            }
        }
        if !merged {
            // We didn't find another set for this capability type. Just append this set to the
            // main list.
            allowlist.push(fragment_type_group);
        }
    }

    Ok(())
}

impl VerifyRoutes {
    /// Creates a new VerifyRoute instance with a `stamp_path` that is written
    /// to if the verification succeeds. A `depfile_path` that lists all the
    /// files this executable touches and a `allowlist_path` which lists all
    /// the moniker & protocol pairs that are filtered from the analysis.
    fn new<S: Into<String>>(
        build_path: S,
        repository_path: S,
        capability_types: Vec<S>,
        response_level: S,
        stamp_path: Option<S>,
        depfile_path: Option<S>,
        allowlist_paths: Vec<S>,
    ) -> Self {
        Self {
            build_path: build_path.into(),
            repository_path: repository_path.into(),
            capability_types: capability_types.into_iter().map(|s| s.into()).collect(),
            response_level: response_level.into(),
            stamp_path: stamp_path.map(|stamp_path| stamp_path.into()),
            depfile_path: depfile_path.map(|depfile_path| depfile_path.into()),
            allowlist_paths: allowlist_paths.into_iter().map(|s| s.into()).collect(),
        }
    }

    /// Launches Scrutiny and performs the capability route analysis. The
    /// results are then filtered based on the provided allowlist and any
    /// errors that are not allowlisted cause the verification to fail listing
    /// all non-allowlisted errors.
    fn verify(&self) -> Result<()> {
        let mut config = Config::run_command_with_plugins(
            CommandBuilder::new("verify.capability_routes")
                .param("capability_types", self.capability_types.join(" "))
                .param("response_level", &self.response_level)
                .build(),
            vec!["DevmgrConfigPlugin", "StaticPkgsPlugin", "CorePlugin", "VerifyPlugin"],
        );
        config.runtime.model.build_path = self.build_path.clone().into();
        config.runtime.model.repository_path = self.repository_path.clone().into();
        config.runtime.logging.silent_mode = true;

        let results = launcher::launch_from_config(config).context("Failed to launch scrutiny")?;
        let route_analysis: CapabilityRouteResults = serde_json5::from_str(&results)
            .context(format!("Failed to deserialize verify routes results: {}", results))?;
        let mut allowlist = vec![];
        for allowlist_path in &self.allowlist_paths {
            let allowlist_fragment = serde_json5::from_str(
                &fs::read_to_string(allowlist_path).context("Failed to read allowlist")?,
            )
            .context("Failed to deserialize allowlist")?;
            merge_allowlists(&mut allowlist, allowlist_fragment)
                .context("failed to merge allowlists")?;
        }
        let filtered_analysis = VerifyRoutes::filter_analysis(route_analysis.results, allowlist);
        for entry in filtered_analysis.iter() {
            if !entry.results.errors.is_empty() {
                bail!(
                    "
Static Capability Flow Analysis Error:
The route verifier failed to verify all capability routes in this build.

See https://fuchsia.dev/fuchsia-src/development/components/v2/troubleshooting#static-analyzer

If the broken route is required for a transition it can be temporarily added
to the allowlist located at: {:?}

Verification Errors:
{}",
                    self.allowlist_paths,
                    serde_json::to_string_pretty(&filtered_analysis).unwrap()
                );
            }
        }

        // Write out the depfile.
        if let Some(depfile_path) = self.depfile_path.as_ref() {
            let stamp_path = self
                .stamp_path
                .as_ref()
                .ok_or(anyhow!("Cannot specify depfile without specifying stamp"))?;

            let deps: Vec<String> = route_analysis.deps.into_iter().collect();
            let mut depfile =
                fs::File::create(depfile_path).context("Failed to create dep file")?;
            write!(depfile, "{}: {}", stamp_path, deps.join(" "))
                .context("Failed to write to dep file")?;

            fs::write(stamp_path, "Verified\n").context("Failed to write stamp file")?;
        }
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

#[ffx_plugin()]
pub async fn scrutiny_routes(cmd: ScrutinyRoutesCommand) -> Result<(), Error> {
    if cmd.depfile.is_some() && cmd.stamp.is_none() {
        bail!("Cannot specify --depfile without --stamp");
    }

    // argh(default = "vec![...]") does not work due to failed trait bound:
    // FromStr on Vec<_>. Apply default when vec is empty.
    let capability_types = if cmd.capability_type.len() > 0 {
        cmd.capability_type
    } else {
        default_capability_types()
    }
    .into_iter()
    .map(|capability_type| capability_type.into())
    .collect();

    let verify_routes = VerifyRoutes::new(
        cmd.build_path,
        cmd.repository_path,
        capability_types,
        cmd.response_level.into(),
        cmd.stamp,
        cmd.depfile,
        cmd.allowlist,
    );
    verify_routes.verify()?;

    Ok(())
}
