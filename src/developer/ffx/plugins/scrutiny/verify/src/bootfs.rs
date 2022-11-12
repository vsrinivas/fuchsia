// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    ffx_scrutiny_verify_args::bootfs::Command,
    scrutiny_config::{Config, LoggingConfig, ModelConfig, PluginConfig, RuntimeConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_utils::{
        bootfs::BootfsPackageIndex,
        golden::{CompareResult, GoldenFile},
    },
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

pub async fn verify(cmd: &Command, tmp_dir: Option<&PathBuf>) -> Result<HashSet<PathBuf>> {
    if cmd.golden.len() == 0 {
        bail!("Must specify at least one --golden");
    }
    let mut deps = HashSet::new();
    let zbi_path = &cmd.zbi;
    let zbi = zbi_path
        .to_str()
        .ok_or_else(|| anyhow!("Failed to convert ZBI path to string: {:?}", cmd.zbi))?;
    deps.insert(zbi_path.clone());

    let config = Config::run_command_with_runtime(
        CommandBuilder::new("tool.zbi.list.bootfs").param("input", zbi).build(),
        RuntimeConfig {
            logging: LoggingConfig { silent_mode: true, ..LoggingConfig::minimal() },
            plugin: PluginConfig { plugins: vec!["ToolkitPlugin".to_string()] },
            model: match tmp_dir {
                Some(tmp_dir) => ModelConfig::minimal().with_temporary_directory(tmp_dir.clone()),
                None => ModelConfig::minimal(),
            },
            ..RuntimeConfig::minimal()
        },
    );
    let scrutiny_output =
        launcher::launch_from_config(config).context("Failed to launch scrutiny")?;

    let unscrutinized_dirs = std::collections::HashSet::from([
        // blob dir files are unscrutinized, because their presence is dictated
        // by the boot package index, which itself is scrutinized the same way
        // static_packages is scrutinized.
        Some(Path::new("blob")),
    ]);

    let bootfs_files: Vec<String> = serde_json::from_str(&scrutiny_output)
        .context(format!("Failed to deserialize scrutiny output: {}", scrutiny_output))?;
    let total_bootfs_file_count = bootfs_files.len();

    let non_blob_files = bootfs_files
        .into_iter()
        .filter(|filename| !unscrutinized_dirs.contains(&Path::new(filename).parent()))
        .collect::<Vec<String>>();

    for golden_file_path in cmd.golden.iter() {
        let golden_file =
            GoldenFile::open(&golden_file_path).context("Failed to open the golden file")?;
        match golden_file.compare(non_blob_files.clone()) {
            CompareResult::Matches => Ok(()),
            CompareResult::Mismatch { errors } => {
                println!("Bootfs file mismatch\n");
                for error in errors.iter() {
                    println!("{}", error);
                }
                println!(
                    "\nIf you intended to change the bootfs contents, please acknowledge it by updating {:?} with the added or removed lines",
                    golden_file_path,
                );
                println!("{}", SOFT_TRANSITION_MSG);
                Err(anyhow!("bootfs file mismatch"))
            }
        }?;

        deps.insert(golden_file_path.clone());
    }

    // Extract the bootfs package index.
    let config = Config::run_command_with_runtime(
        CommandBuilder::new("tool.zbi.extract.bootfs.packages").param("input", zbi).build(),
        RuntimeConfig {
            logging: LoggingConfig { silent_mode: true, ..LoggingConfig::minimal() },
            plugin: PluginConfig { plugins: vec!["ToolkitPlugin".to_string()] },
            ..RuntimeConfig::minimal()
        },
    );
    let scrutiny_output =
        launcher::launch_from_config(config).context("Failed to launch scrutiny")?;

    let bootfs_packages: BootfsPackageIndex = serde_json::from_str(&scrutiny_output)
        .context(format!("Failed to deserialize scrutiny output: {}", scrutiny_output))?;

    // TODO(fxbug.dev/97517) After the first bootfs package is migrated to a component, an
    // absence of a bootfs package index is an error.
    if bootfs_packages.bootfs_pkgs.is_none() {
        if non_blob_files.len() != total_bootfs_file_count {
            return Err(anyhow!("tool.zbi.extract.bootfs.packages returned empty result, but there were blobs in the bootfs."));
        } else {
            return Ok(deps);
        }
    }

    // Extract package names from bootfs package descriptions.
    let bootfs_packages = bootfs_packages.bootfs_pkgs.unwrap();
    let bootfs_package_names: Vec<String> = bootfs_packages
        .into_iter()
        .map(|((name, _variant), _hash)| name.as_ref().to_string())
        .collect();

    for golden_packages_path in cmd.golden_packages.iter() {
        let golden_packages = GoldenFile::open(&golden_packages_path)
            .context("Failed to open the golden packages list")?;

        match golden_packages.compare(bootfs_package_names.clone()) {
            CompareResult::Matches => Ok(()),
            CompareResult::Mismatch { errors } => {
                println!("Bootfs package index mismatch \n");
                for error in errors.iter() {
                    println!("{}", error);
                }
                println!(
                    "\nIf you intended to change the bootfs contents, please acknowledge it by updating {:?} with the added or removed lines.",
                    golden_packages_path,
                );
                println!("{}", SOFT_TRANSITION_MSG);
                Err(anyhow!("bootfs package index mismatch"))
            }
        }?;
        deps.insert(golden_packages_path.clone());
    }

    Ok(deps)
}
