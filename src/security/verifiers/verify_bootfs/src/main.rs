// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    clap::{App, Arg},
    scrutiny_config::Config,
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_utils::golden::{CompareResult, GoldenFile},
    serde_json,
    std::fs,
};

const SOFT_TRANSITION_MSG : &str = "
If you are making a change in fuchsia.git that causes this, you need to perform a soft transition:
1: Instead of adding lines as written above, add each line prefixed with a question mark to mark it as transitional.
2: Instead of removing lines as written above, prefix the line with a question mark to mark it as transitional.
3: Check in your fuchsia.git change.
4: For each new line you added in 1, remove the question mark.
5: For each existing line you modified in 2, remove the line.
";

#[allow(dead_code)]
struct VerifyBootfs {
    golden_path: String,
    zbi_path: String,
}

impl VerifyBootfs {
    pub fn new(golden_path: String, zbi_path: impl Into<String>) -> Self {
        Self { golden_path, zbi_path: zbi_path.into() }
    }

    /// Extracts the bootfs files from the provided zbi_path and compares
    /// it against the golden file.
    pub fn verify(&self) -> Result<()> {
        let config = Config::run_command_with_plugins(
            CommandBuilder::new("tool.zbi.list.bootfs")
                .param("input", self.zbi_path.clone())
                .build(),
            vec!["ToolkitPlugin"],
        );
        let bootfs_files: Vec<String> = serde_json::from_str(
            &launcher::launch_from_config(config).context("failed to launch scrutiny")?,
        )
        .context("failed to deserialize scrutiny output")?;
        let golden_file =
            GoldenFile::open(self.golden_path.clone()).context("failed to open the golden file")?;
        match golden_file.compare(bootfs_files) {
            CompareResult::Matches => Ok(()),
            CompareResult::Mismatch { errors } => {
                println!("Bootfs file mismatch");
                println!("");
                for error in errors.iter() {
                    println!("{}", error);
                }
                println!("");
                println!("If you intended to change the bootfs contents, please acknowledge it by updating {} with the added or removed lines.", self.golden_path);
                println!("{}", SOFT_TRANSITION_MSG);
                Err(anyhow!("bootfs file mismatch"))
            }
        }
    }
}

fn main() -> Result<()> {
    simplelog::SimpleLogger::init(simplelog::LevelFilter::Error, simplelog::Config::default())?;
    let args = App::new("scrutiny_verify_bootfs")
        .version("1.0")
        .author("Fuchsia Authors")
        .about("Check the bootfs file list extracted from the ZBI against a golden file.")
        .arg(
            Arg::with_name("stamp")
                .long("stamp")
                .required(true)
                .help("The stamp file output location.")
                .value_name("stamp")
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
    let zbi_path = args.value_of("zbi").expect("failed to find the zbi path");

    let mut last_error: Result<()> = Ok(());
    for golden_file in golden_files.iter() {
        let verifier = VerifyBootfs::new(golden_file.clone(), zbi_path.clone());
        match verifier.verify() {
            Ok(()) => {
                fs::write(args.value_of("stamp").unwrap(), "Verified")
                    .expect("Failed to write stamp file");
                return Ok(());
            }
            error => {
                last_error = error;
            }
        }
    }
    last_error
}
