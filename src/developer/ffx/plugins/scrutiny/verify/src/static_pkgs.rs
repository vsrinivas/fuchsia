// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    ffx_scrutiny_verify_args::static_pkgs::Command,
    scrutiny_config::{ConfigBuilder, ModelConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_plugins::static_pkgs::StaticPkgsCollection,
    scrutiny_utils::golden::{CompareResult, GoldenFile},
    std::{collections::HashSet, path::PathBuf},
};

const SOFT_TRANSITION_MSG : &str = "
If you are making a change in fuchsia.git that causes this, you need to perform a soft transition:
1: Instead of adding lines as written above, add each line prefixed with a question mark to mark it as transitional.
2: Instead of removing lines as written above, prefix the line with a question mark to mark it as transitional.
3: Check in your fuchsia.git change.
4: For each new line you added in 1, remove the question mark.
5: For each existing line you modified in 2, remove the line.
";

struct Query {
    product_bundle: PathBuf,
}

fn verify_static_pkgs(query: &Query, golden_file_path: PathBuf) -> Result<HashSet<PathBuf>> {
    let command = CommandBuilder::new("static.pkgs").build();
    let plugins = vec!["DevmgrConfigPlugin".to_string(), "StaticPkgsPlugin".to_string()];
    let model = ModelConfig::from_product_bundle(query.product_bundle.clone())?;
    let mut config = ConfigBuilder::with_model(model).command(command).plugins(plugins).build();
    config.runtime.logging.silent_mode = true;

    let scrutiny_output =
        launcher::launch_from_config(config).context("Failed to run static.pkgs")?;
    let static_pkgs_result: StaticPkgsCollection =
        serde_json::from_str(&scrutiny_output).context(format!(
            "Failed to parse static.pkgs JSON output as structured static packages list: {}",
            scrutiny_output
        ))?;
    if static_pkgs_result.errors.len() > 0 {
        return Err(anyhow!("static.pkgs reported errors: {:#?}", static_pkgs_result.errors));
    }
    if static_pkgs_result.static_pkgs.is_none() {
        return Err(anyhow!("static.pkgs returned empty result"));
    }
    let static_pkgs = static_pkgs_result.static_pkgs.unwrap();

    // Extract package names from static package descriptions.
    let static_package_names: Vec<String> = static_pkgs
        .into_iter()
        .map(|((name, _variant), _hash)| name.as_ref().to_string())
        .collect();

    let golden_contents =
        std::fs::read(golden_file_path.as_path()).context("Failed to read golden file")?;
    let golden_file = GoldenFile::from_contents(golden_file_path.as_path(), golden_contents)
        .context("Failed to parse golden file")?;

    match golden_file.compare(static_package_names) {
        CompareResult::Matches => {
            let mut deps = static_pkgs_result.deps;
            deps.insert(golden_file_path.clone());
            Ok(deps)
        }
        CompareResult::Mismatch { errors } => {
            println!("Static package file mismatch");
            println!("");
            for error in errors.iter() {
                println!("{}", error);
            }
            println!("");
            println!("If you intended to change the static package contents, please acknowledge it by updating {:?} with the added or removed lines.", golden_file_path);
            println!("{}", SOFT_TRANSITION_MSG);
            Err(anyhow!("static file mismatch"))
        }
    }
}

pub async fn verify(cmd: &Command) -> Result<HashSet<PathBuf>> {
    let product_bundle = cmd.product_bundle.clone();
    let query = Query { product_bundle };
    let mut deps = HashSet::new();

    for golden_file_path in cmd.golden.iter() {
        deps.extend(verify_static_pkgs(&query, golden_file_path.clone())?);
    }

    Ok(deps)
}
