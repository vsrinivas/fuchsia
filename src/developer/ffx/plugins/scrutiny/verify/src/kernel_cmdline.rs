// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    ffx_scrutiny_verify_args::kernel_cmdline::Command,
    scrutiny_config::{ConfigBuilder, ModelConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_plugins::zbi::CmdlineCollection,
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
    // A host filesystem path to the product bundle.
    product_bundle: PathBuf,
}

fn verify_kernel_cmdline<P: AsRef<Path>>(query: &Query, golden_path: P) -> Result<()> {
    let command = CommandBuilder::new("zbi.cmdline").build();
    let plugins = vec!["ZbiPlugin".to_string()];
    let model = ModelConfig::from_product_bundle(query.product_bundle.clone())?;
    let mut config = ConfigBuilder::with_model(model).command(command).plugins(plugins).build();
    config.runtime.logging.silent_mode = true;

    let scrutiny_output =
        launcher::launch_from_config(config).context("Failed to launch scrutiny")?;

    let cmdline_collection: CmdlineCollection = serde_json::from_str(&scrutiny_output)
        .context(format!("Failed to deserialize scrutiny output: {}", scrutiny_output))?;
    let cmdline = cmdline_collection.cmdline;
    let golden_file = GoldenFile::open(&golden_path).context("Failed to open golden file")?;
    match golden_file.compare(cmdline) {
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

pub async fn verify(cmd: &Command) -> Result<HashSet<PathBuf>> {
    if cmd.golden.len() == 0 {
        bail!("Must specify at least one --golden");
    }
    let mut deps = HashSet::new();

    let query = Query { product_bundle: cmd.product_bundle.clone() };
    for golden_file_path in cmd.golden.iter() {
        verify_kernel_cmdline(&query, golden_file_path)?;

        deps.insert(golden_file_path.clone());
    }

    Ok(deps)
}
