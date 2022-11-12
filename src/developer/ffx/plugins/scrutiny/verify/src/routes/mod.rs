// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod allowlist;

use {
    allowlist::{AllowlistFilter, UnversionedAllowlist, V0Allowlist, V1Allowlist},
    anyhow::{bail, Context, Result},
    ffx_scrutiny_verify_args::routes::{default_capability_types, Command},
    scrutiny_config::Config,
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_plugins::verify::CapabilityRouteResults,
    scrutiny_utils::path::join_and_canonicalize,
    serde_json,
    std::{collections::HashSet, fs, io::Read, path::PathBuf},
};

struct Query {
    capability_types: Vec<String>,
    response_level: String,
    build_path: PathBuf,
    update_package_path: PathBuf,
    blobfs_paths: Vec<PathBuf>,
    allowlist_paths: Vec<PathBuf>,
    component_tree_config_path: Option<PathBuf>,
    tmp_dir_path: Option<PathBuf>,
}

impl Query {
    fn with_temporary_directory(mut self, tmp_dir_path: Option<&PathBuf>) -> Self {
        self.tmp_dir_path = tmp_dir_path.map(PathBuf::clone);
        self
    }
}

impl From<&Command> for Query {
    fn from(cmd: &Command) -> Self {
        // argh(default = "vec![...]") does not work due to failed trait bound:
        // FromStr on Vec<_>. Apply default when vec is empty.
        let capability_types = if cmd.capability_type.len() > 0 {
            cmd.capability_type.clone()
        } else {
            default_capability_types()
        }
        .into_iter()
        .map(|capability_type| capability_type.into())
        .collect();
        let update_package_path = join_and_canonicalize(&cmd.build_path, &cmd.update);
        let blobfs_paths = cmd
            .blobfs
            .iter()
            .map(|blobfs| join_and_canonicalize(&cmd.build_path, blobfs))
            .collect();
        let allowlist_paths = cmd
            .allowlist
            .iter()
            .map(|allowlist| join_and_canonicalize(&cmd.build_path, allowlist))
            .collect();
        let component_tree_config_path =
            cmd.component_tree_config.as_ref().map(|component_tree_config| {
                join_and_canonicalize(&cmd.build_path, &component_tree_config)
            });
        Query {
            capability_types,
            response_level: cmd.response_level.clone().into(),
            build_path: cmd.build_path.clone(),
            update_package_path,
            blobfs_paths,
            allowlist_paths,
            component_tree_config_path,
            tmp_dir_path: None,
        }
    }
}

fn load_allowlist(allowlist_paths: &Vec<PathBuf>) -> Result<Box<dyn AllowlistFilter>> {
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

pub async fn verify(cmd: &Command, tmp_dir: Option<&PathBuf>) -> Result<HashSet<PathBuf>> {
    let query: Query = Query::from(cmd).with_temporary_directory(tmp_dir);
    let mut config = Config::run_command_with_plugins(
        CommandBuilder::new("verify.capability_routes")
            .param("capability_types", query.capability_types.join(" "))
            .param("response_level", query.response_level)
            .build(),
        vec!["DevmgrConfigPlugin", "StaticPkgsPlugin", "CorePlugin", "VerifyPlugin"],
    );
    config.runtime.model.build_path = query.build_path;
    config.runtime.model.update_package_path = query.update_package_path;
    config.runtime.model.blobfs_paths = query.blobfs_paths;
    config.runtime.model.component_tree_config_path = query.component_tree_config_path;
    config.runtime.model.tmp_dir_path = query.tmp_dir_path;
    config.runtime.logging.silent_mode = true;

    let results = launcher::launch_from_config(config).context("Failed to launch scrutiny")?;
    let route_analysis: CapabilityRouteResults = serde_json5::from_str(&results)
        .context(format!("Failed to deserialize verify routes results: {}", results))?;

    let allowlist_filter = load_allowlist(&query.allowlist_paths)
        .context("Failed to parse all allowlist fragments from supported format")?;

    let filtered_analysis = allowlist_filter.filter_analysis(route_analysis.results);
    for entry in filtered_analysis.iter() {
        if !entry.results.errors.is_empty() {
            bail!(
                "
Static Capability Flow Analysis Error:
The route verifier failed to verify all capability routes in this build.

See https://fuchsia.dev/go/components/static-analysis-errors

If the broken route is required for a transition it can be temporarily added
to the allowlist located at: {:?}

Verification Errors:
{}",
                query.allowlist_paths,
                serde_json::to_string_pretty(&filtered_analysis).unwrap()
            );
        }
    }

    Ok(route_analysis.deps)
}
