// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    ffx_scrutiny_verify_args::kernel_cmdline::Command,
    scrutiny_config::{Config, LoggingConfig, ModelConfig, PluginConfig, RuntimeConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_utils::golden::{CompareResult, GoldenFile},
    serde_json,
    std::{
        collections::HashSet,
        path::{Path, PathBuf},
    },
};

const SOFT_TRANSITION_MSG : &str = "
If you are making a change in fuchsia.git that causes this, you need to perform a soft transition:
1: Instead of adding lines as written above, add each line prefixed with a question mark to mark it as transitional.
2: Instead of removing lines as written above, prefix the line with a question mark to mark it as transitional.
3: Check in your fuchsia.git change.
4: For each new line you added in 1, remove the question mark.
5: For each existing line you modified in 2, remove the line.
";

// Query information common to multiple verification passes that may run against different golden
// files.
struct Query {
    // A host filesystem path to the ZBI blob.
    zbi_path: PathBuf,
    // A root directory for temporary files created by scrutiny.
    tmp_dir_path: Option<PathBuf>,
}

fn verify_kernel_cmdline<P: AsRef<Path>>(query: &Query, golden_path: P) -> Result<()> {
    let zbi_path = query.zbi_path.to_str().ok_or_else(|| {
        anyhow!(
            "ZBI path {:?} cannot be converted to string for passing to scrutiny",
            query.zbi_path
        )
    })?;
    let config = Config::run_command_with_runtime(
        CommandBuilder::new("tool.zbi.extract.cmdline").param("input", zbi_path).build(),
        RuntimeConfig {
            logging: LoggingConfig { silent_mode: true, ..LoggingConfig::minimal() },
            plugin: PluginConfig { plugins: vec!["ToolkitPlugin".to_string()] },
            model: match &query.tmp_dir_path {
                Some(tmp_dir_path) => {
                    ModelConfig::minimal().with_temporary_directory(tmp_dir_path.clone())
                }
                None => ModelConfig::minimal(),
            },
            ..RuntimeConfig::minimal()
        },
    );
    let scrutiny_output =
        launcher::launch_from_config(config).context("Failed to launch scrutiny")?;

    let kernel_cmdline: String = serde_json::from_str(&scrutiny_output)
        .context(format!("Failed to deserialize scrutiny output: {}", scrutiny_output))?;
    let mut sorted_cmdline =
        kernel_cmdline.split(' ').map(ToString::to_string).collect::<Vec<String>>();
    sorted_cmdline.sort();
    let golden_file = GoldenFile::open(&golden_path).context("Failed to open golden file")?;
    match golden_file.compare(sorted_cmdline) {
        CompareResult::Matches => Ok(()),
        CompareResult::Mismatch { errors } => {
            println!("Kernel cmdline mismatch");
            println!("");
            for error in errors.iter() {
                println!("{}", error);
            }
            println!("");
            println!(
                "If you intended to change the kernel command line, please acknowledge it by updating {:?} with the added or removed lines.",
                golden_path.as_ref()
            );
            println!("{}", SOFT_TRANSITION_MSG);
            Err(anyhow!("kernel cmdline mismatch"))
        }
    }
}

pub async fn verify(cmd: &Command, tmp_dir: Option<&PathBuf>) -> Result<HashSet<PathBuf>> {
    if cmd.golden.len() == 0 {
        bail!("Must specify at least one --golden");
    }
    let mut deps = HashSet::new();
    deps.insert(cmd.zbi.clone());

    let query = Query { zbi_path: cmd.zbi.clone(), tmp_dir_path: tmp_dir.map(PathBuf::clone) };
    for golden_file_path in cmd.golden.iter() {
        verify_kernel_cmdline(&query, golden_file_path)?;

        deps.insert(golden_file_path.clone());
    }

    Ok(deps)
}
