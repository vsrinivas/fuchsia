// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    clap::{App, Arg},
    scrutiny_config::{Config, LoggingConfig, ModelConfig, PluginConfig, RuntimeConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_plugins::static_pkgs::StaticPkgsCollection,
    scrutiny_utils::{
        artifact::{ArtifactReader, FileArtifactReader},
        golden::{CompareResult, GoldenFile},
    },
    serde_json,
    std::{
        fs::{self, File},
        io::Write,
        path::PathBuf,
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

struct VerifyStaticPkgs {
    build_path: String,
    golden_path: String,
    zbi_path: String,
    depfile_path: String,
    manifest_path: String,
    stamp_path: String,
}

impl VerifyStaticPkgs {
    pub fn new(
        build_path: String,
        golden_path: String,
        zbi_path: impl Into<String>,
        depfile_path: String,
        manifest_path: String,
        stamp_path: String,
    ) -> Self {
        Self {
            build_path,
            golden_path,
            zbi_path: zbi_path.into(),
            depfile_path,
            manifest_path,
            stamp_path,
        }
    }

    /// The static package verifier extracts the system_image_hash from the devmgr config
    /// inside the ZBI. It uses this to extract the static_packages_hash which contains
    /// the list of static_packages. This list is compared against the golden file.
    pub fn verify(&self) -> Result<()> {
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
            golden_reader.read_raw(&self.golden_path).context("Failed to read golden file")?;
        let golden_file = GoldenFile::from_contents(self.golden_path.clone(), golden_contents)
            .context("Failed to parse golden file")?;
        match golden_file.compare(static_package_urls) {
            CompareResult::Matches => {
                // Write out the depfile.
                let deps: Vec<String> = static_pkgs_result
                    .deps
                    .union(&golden_reader.get_deps())
                    .map(String::from)
                    .collect();
                let mut depfile = File::create(&self.depfile_path)
                    .context(format!("Failed to create dep file: {}", &self.depfile_path))?;
                write!(depfile, "{}: {}", self.stamp_path, deps.join(" "))
                    .context(format!("Failed to write dep file: {}", &self.depfile_path))?;
                Ok(())
            }
            CompareResult::Mismatch { errors } => {
                println!("Static package file mismatch");
                println!("");
                for error in errors.iter() {
                    println!("{}", error);
                }
                println!("");
                println!("If you intended to change the static package contents, please acknowledge it by updating {} with the added or removed lines.", self.golden_path);
                println!("{}", SOFT_TRANSITION_MSG);
                Err(anyhow!("static file mismatch"))
            }
        }
    }
}

fn main() -> Result<()> {
    simplelog::SimpleLogger::init(simplelog::LevelFilter::Error, simplelog::Config::default())?;
    let args = App::new("scrutiny_static_pkgs")
        .version("1.0")
        .author("Fuchsia Authors")
        .about("Check the static packages extracted from the ZBI against a golden file.")
        .arg(
            Arg::with_name("build-path")
                .long("build-path")
                .required(true)
                .help("The root build directory for all build artifacts.")
                .value_name("build-path")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("stamp")
                .long("stamp")
                .required(true)
                .help("The stamp file output location.")
                .value_name("stamp")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("depfile")
                .long("depfile")
                .required(true)
                .help("The depfile output location.")
                .value_name("depfile")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("manifest")
                .long("manifest")
                .required(true)
                .help("The blobfs manifest input location.")
                .value_name("manifest")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("goldens")
                .long("goldens")
                .required(true)
                .help(
                    "Path to one of the possible golden files to check against,
                       there should only be one golden file in normal case, and only
                       two golden files, one old file and one new file during a soft
                       transition. After the transition, the old golden file should
                       be removed and only leave the new golden file.",
                )
                .value_name("goldens")
                .takes_value(true)
                .min_values(1)
                .max_values(2),
        )
        .arg(
            Arg::with_name("zbi")
                .long("zbi")
                .required(true)
                .help("Path to the ZBI to verify.")
                .value_name("zbi")
                .takes_value(true),
        )
        .get_matches();
    let golden_files: Vec<String> = args
        .values_of("goldens")
        .expect("failed to find goldens")
        .map(ToString::to_string)
        .collect();
    let build_path = args.value_of("build-path").expect("failed to find build path");
    let zbi_path = args.value_of("zbi").expect("failed to find the zbi path");
    let depfile_path = args.value_of("depfile").expect("failed to find the depfile path");
    let manifest_path = args.value_of("manifest").expect("failed to find the blobfs manifest path");
    let stamp_path = args.value_of("stamp").expect("failed to find the stamp path");

    let mut last_error: Result<()> = Ok(());
    for golden_file in golden_files.iter() {
        let verifier = VerifyStaticPkgs::new(
            build_path.to_string(),
            golden_file.clone(),
            zbi_path.clone(),
            depfile_path.to_string(),
            manifest_path.to_string(),
            stamp_path.to_string(),
        );
        match verifier.verify() {
            Ok(()) => {
                fs::write(stamp_path, "Verified").expect("Failed to write stamp file");
                return Ok(());
            }
            error => {
                last_error = error;
            }
        }
    }
    last_error
}
