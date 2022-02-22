// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod allowlist;

use {
    crate::allowlist::{AllowlistFilter, UnversionedAllowlist, V0Allowlist, V1Allowlist},
    anyhow::{anyhow, bail, Context, Error, Result},
    ffx_core::ffx_plugin,
    ffx_scrutiny_routes_args::{default_capability_types, ScrutinyRoutesCommand},
    scrutiny_config::Config,
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_plugins::verify::CapabilityRouteResults,
    serde_json,
    std::{
        fs,
        io::{Read, Write},
    },
};

fn load_allowlist(allowlist_paths: &Vec<String>) -> Result<Box<dyn AllowlistFilter>> {
    let builders = vec![UnversionedAllowlist::new(), V0Allowlist::new(), V1Allowlist::new()];
    let mut err = None;

    for mut builder in builders.into_iter() {
        for path in allowlist_paths.iter() {
            let reader: Box<dyn Read> =
                Box::new(fs::File::open(path).context("Failed to open allowlist fragment")?);
            if let Err(load_err) = builder.load(reader) {
                err = Some(load_err);
                break;
            }
        }
        if err.is_none() {
            return Ok(builder.build());
        }
    }

    Err(err.unwrap())
}

pub struct VerifyRoutes {
    build_path: String,
    repository_path: String,
    capability_types: Vec<String>,
    response_level: String,
    stamp_path: Option<String>,
    depfile_path: Option<String>,
    allowlist_paths: Vec<String>,
    component_tree_config_path: Option<String>,
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
        component_tree_config_path: Option<S>,
    ) -> Self {
        Self {
            build_path: build_path.into(),
            repository_path: repository_path.into(),
            capability_types: capability_types.into_iter().map(|s| s.into()).collect(),
            response_level: response_level.into(),
            stamp_path: stamp_path.map(|stamp_path| stamp_path.into()),
            depfile_path: depfile_path.map(|depfile_path| depfile_path.into()),
            allowlist_paths: allowlist_paths.into_iter().map(|s| s.into()).collect(),
            component_tree_config_path: component_tree_config_path
                .map(|component_tree_config_path| component_tree_config_path.into()),
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
        config.runtime.model.component_tree_config_path = self
            .component_tree_config_path
            .clone()
            .map(|component_tree_config_path| component_tree_config_path.into());
        config.runtime.logging.silent_mode = true;

        let results = launcher::launch_from_config(config).context("Failed to launch scrutiny")?;
        let route_analysis: CapabilityRouteResults = serde_json5::from_str(&results)
            .context(format!("Failed to deserialize verify routes results: {}", results))?;

        let allowlist_filter = load_allowlist(&self.allowlist_paths)
            .context("Failed to parse all allowlist fragments from supported format")?;

        let filtered_analysis = allowlist_filter.filter_analysis(route_analysis.results);
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
        cmd.component_tree_config,
    );
    verify_routes.verify()?;

    Ok(())
}
