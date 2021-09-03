// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    clap::{App, Arg},
    regex::Regex,
    scrutiny_config::Config,
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_utils::{
        golden::{CompareResult, GoldenFile},
        key_value::parse_key_value,
    },
    serde_json,
    std::{
        collections::HashMap,
        fs::{self, File},
        io::Write,
        path::Path,
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
    golden_path: String,
    zbi_path: String,
    depfile_path: String,
    manifest_path: String,
    stamp_path: String,
}

impl VerifyStaticPkgs {
    pub fn new(
        golden_path: String,
        zbi_path: impl Into<String>,
        depfile_path: String,
        manifest_path: String,
        stamp_path: String,
    ) -> Self {
        Self { golden_path, zbi_path: zbi_path.into(), depfile_path, manifest_path, stamp_path }
    }

    /// The static package verifier extracts the system_image_hash from the devmgr config
    /// inside the ZBI. It uses this to extract the static_packages_hash which contains
    /// the list of static_packages. This list is compared against the golden file.
    pub fn verify(&self) -> Result<()> {
        let mut deps: Vec<String> = Vec::new();
        let config = Config::run_command_with_plugins(
            CommandBuilder::new("tool.zbi.extract.devmgr")
                .param("input", self.zbi_path.clone())
                .build(),
            vec!["ToolkitPlugin"],
        );

        // Retrieve the system_image merkle from the devmgr configuration.
        let devmgr_config: HashMap<String, String> = parse_key_value(serde_json::from_str(
            &launcher::launch_from_config(config)
                .context("failed to run tool.zbi.extract.devmgr")?,
        )?)
        .context("failed to parse devmgr config")?;
        let system_image_merkle = devmgr_config
            .get("zircon.system.pkgfs.cmd")
            .ok_or(anyhow!("failed to find zircon.system.pkgfs.cmd in bootfs devmgr config"))?
            .replace("bin/pkgsvr+", "");

        // Extract the system_image package.
        let blob_manifest = parse_key_value(
            fs::read_to_string(&self.manifest_path).context("failed to read manifest_path")?,
        )
        .context("failed to parse blobfs manifest")?;
        let blob_directory = Path::new(&self.manifest_path)
            .parent()
            .context("blob_manifest isn't in a directory")?;
        let system_image_blob_path = blob_directory
            .join(
                &blob_manifest
                    .get(&system_image_merkle)
                    .ok_or(anyhow!("Couldn't find system_image_merkle in blob_manifest"))?,
            )
            .into_os_string()
            .into_string()
            .map_err(|_| anyhow!("failed to convert system_image_path to a string"))?;
        deps.push(system_image_blob_path.clone());
        let config = Config::run_command_with_plugins(
            CommandBuilder::new("tool.far.extract.meta")
                .param("input", system_image_blob_path)
                .build(),
            vec!["ToolkitPlugin"],
        );
        let system_image_meta: HashMap<String, String> = serde_json::from_str(
            &launcher::launch_from_config(config).context("failed to run tool.far.extract.meta")?,
        )?;

        // Retrieve the static_package merkle from the meta/contents.
        let meta_contents = parse_key_value(system_image_meta["meta/contents"].clone())
            .context("failed to parse meta/contents from system_image")?;
        let static_package_merkle = meta_contents
            .get("data/static_packages")
            .context("no static_package in system_image")?
            .clone();
        let static_package_blob_path = blob_directory
            .join(
                &blob_manifest
                    .get(&static_package_merkle)
                    .ok_or(anyhow!("No static_package_merkle in blob_manifest"))?,
            )
            .into_os_string()
            .into_string()
            .map_err(|_| anyhow!("failed to convert static_package_blob path to string"))?;
        deps.push(static_package_blob_path.clone());

        // Extract all of the static packages which are in the form
        // fuchsia-pkg://fuchsia.com/foo/0=merkle_root.
        let full_static_package_urls: Vec<String> = fs::read_to_string(static_package_blob_path)
            .context("failed to read static package blob path")?
            .trim()
            .split("\n")
            .map(String::from)
            .collect();
        // Shortened names in the form of fuchsia-pkg://fuchsia.com/foo.
        let mut static_package_urls: Vec<String> = Vec::new();
        let re = Regex::new(r"/[0-9]=[0-9a-f]+").unwrap();
        for full_url in full_static_package_urls.iter() {
            let short_url = re.replace_all(full_url, "");
            static_package_urls.push(short_url.to_string());
        }

        // Write out the depfile.
        let mut depfile =
            File::create(&self.depfile_path).context("failed to read depfile_path")?;
        write!(depfile, "{}: {}", self.stamp_path, deps.join(" "))
            .context("failed to write to depfile_path")?;

        let golden_file =
            GoldenFile::open(self.golden_path.clone()).context("failed to read golden_path")?;
        match golden_file.compare(static_package_urls) {
            CompareResult::Matches => Ok(()),
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
    let zbi_path = args.value_of("zbi").expect("failed to find the zbi path");
    let depfile_path = args.value_of("depfile").expect("failed to find the depfile path");
    let manifest_path = args.value_of("manifest").expect("failed to find the blobfs manifest path");
    let stamp_path = args.value_of("stamp").expect("failed to find the stamp path");

    let mut last_error: Result<()> = Ok(());
    for golden_file in golden_files.iter() {
        let verifier = VerifyStaticPkgs::new(
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
