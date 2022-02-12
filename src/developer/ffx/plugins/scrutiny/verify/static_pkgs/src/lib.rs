// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Error, Result},
    ffx_core::ffx_plugin,
    ffx_scrutiny_static_pkgs_args::ScrutinyStaticPkgsCommand,
    scrutiny_config::{Config, LoggingConfig, ModelConfig, PluginConfig, RuntimeConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_plugins::static_pkgs::StaticPkgsCollection,
    scrutiny_utils::{
        artifact::{ArtifactReader, FileArtifactReader},
        golden::{CompareResult, GoldenFile},
    },
    std::{collections::HashSet, fs, path::PathBuf},
};

const SOFT_TRANSITION_MSG : &str = "
If you are making a change in fuchsia.git that causes this, you need to perform a soft transition:
1: Instead of adding lines as written above, add each line prefixed with a question mark to mark it as transitional.
2: Instead of removing lines as written above, prefix the line with a question mark to mark it as transitional.
3: Check in your fuchsia.git change.
4: For each new line you added in 1, remove the question mark.
5: For each existing line you modified in 2, remove the line.
";

struct VerifyStaticPkgs {
    build_path: String,
    zbi_path: String,
    manifest_path: String,
}

impl VerifyStaticPkgs {
    pub fn new(
        build_path: impl Into<String>,
        zbi_path: impl Into<String>,
        manifest_path: impl Into<String>,
    ) -> Self {
        Self {
            build_path: build_path.into(),
            zbi_path: zbi_path.into(),
            manifest_path: manifest_path.into(),
        }
    }

    /// The static package verifier extracts the system_image_hash from the devmgr config
    /// inside the ZBI. It uses this to extract the static_packages_hash which contains
    /// the list of static_packages. This list is compared against the golden file.
    pub fn verify(&self, golden_path: String) -> Result<HashSet<String>> {
        let build_path = PathBuf::from(self.build_path.clone());
        let config = Config::run_command_with_runtime(
            CommandBuilder::new("static.pkgs").build(),
            RuntimeConfig {
                model: ModelConfig {
                    build_path: build_path.clone(),
                    zbi_path: self.zbi_path.clone(),
                    blob_manifest_path: PathBuf::from(self.manifest_path.clone()),
                    ..ModelConfig::minimal()
                },
                logging: LoggingConfig { silent_mode: true, ..LoggingConfig::minimal() },
                plugin: PluginConfig {
                    plugins: vec!["DevmgrConfigPlugin".to_string(), "StaticPkgsPlugin".to_string()],
                },
                ..RuntimeConfig::minimal()
            },
        );

        let scrutiny_output =
            launcher::launch_from_config(config).context("Failed to run static.pkgs")?;
        let static_pkgs_result: StaticPkgsCollection = serde_json::from_str(&scrutiny_output)
            .context(format!(
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

        // Drop trailing "/0" from package URLs; skip any that do not follow this convention.
        let static_package_urls: Vec<String> = static_pkgs
            .into_iter()
            .filter_map(|(mut url, _)| {
                if url.ends_with("/0") {
                    url.truncate(url.len() - 2);
                    Some(url)
                } else {
                    None
                }
            })
            .collect();

        let mut golden_reader = FileArtifactReader::new(&build_path, &build_path);
        let golden_contents =
            golden_reader.read_raw(&golden_path).context("Failed to read golden file")?;
        let golden_file = GoldenFile::from_contents(golden_path.clone(), golden_contents)
            .context("Failed to parse golden file")?;
        match golden_file.compare(static_package_urls) {
            CompareResult::Matches => Ok(static_pkgs_result
                .deps
                .union(&golden_reader.get_deps())
                .map(String::from)
                .collect()),
            CompareResult::Mismatch { errors } => {
                println!("Static package file mismatch");
                println!("");
                for error in errors.iter() {
                    println!("{}", error);
                }
                println!("");
                println!("If you intended to change the static package contents, please acknowledge it by updating {} with the added or removed lines.", golden_path);
                println!("{}", SOFT_TRANSITION_MSG);
                Err(anyhow!("static file mismatch"))
            }
        }
    }
}

#[ffx_plugin()]
pub async fn scrutiny_static_pkgs(cmd: ScrutinyStaticPkgsCommand) -> Result<(), Error> {
    if cmd.depfile.is_some() && cmd.stamp.is_none() {
        bail!("Cannot specify --depfile without --stamp");
    }

    let verifier = VerifyStaticPkgs::new(&cmd.build_path, &cmd.zbi, &cmd.blobfs_manifest);

    let mut deps = HashSet::new();
    for golden in cmd.golden.into_iter() {
        deps = deps.union(&verifier.verify(golden)?).map(String::from).collect();
    }
    let deps: Vec<String> = deps.into_iter().collect();

    if let Some(depfile_path) = cmd.depfile.as_ref() {
        let stamp_path =
            cmd.stamp.as_ref().ok_or(anyhow!("Cannot specify depfile without specifying stamp"))?;
        fs::write(depfile_path, &format!("{}: {}", stamp_path, deps.join(" ")))
            .context(format!("Failed to write dep file: {}", depfile_path))?;
        fs::write(stamp_path, "Verified").context("Failed to write stamp file")?;
    }

    Ok(())
}
