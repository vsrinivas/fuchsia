// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use serde::Deserialize;
use std::path::PathBuf;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "assembly", description = "Assemble images")]
pub struct AssemblyCommand {
    /// the assembly operation to perform
    #[argh(subcommand)]
    pub op_class: OperationClass,
}

/// This is the set of top-level operations within the `ffx assembly` plugin
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum OperationClass {
    Image(ImageArgs),
    CreateSystem(CreateSystemArgs),
    CreateUpdate(CreateUpdateArgs),
    ConfigData(ConfigDataArgs),
    Product(ProductArgs),
    SizeCheck(SizeCheckArgs),
}

/// perform the assembly of images
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "image")]
pub struct ImageArgs {
    /// the configuration file(s) that specifies the packages, binaries, and
    /// settings specific to the product being assembled.  If multiple files are
    /// provided, they will be merged.  Only one can provide a kernel
    /// configuration.
    #[argh(option)]
    pub product: Vec<PathBuf>,

    /// the configuration file that specifies the packages, binaries, and
    /// settings specific to the board being assembled.
    #[argh(option)]
    pub board: PathBuf,

    /// log the external commands to gendir as `commands_log.json`.
    #[argh(switch)]
    pub log_commands: bool,

    /// the directory to write assembled outputs to.
    #[argh(option)]
    pub outdir: PathBuf,

    /// the directory to write generated intermediate files to.
    #[argh(option)]
    pub gendir: Option<PathBuf>,
}

/// create the system images.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "create-system")]
pub struct CreateSystemArgs {
    /// the configuration file that specifies which images to generate and how.
    #[argh(option)]
    pub images: PathBuf,

    /// the directory to write assembled outputs to.
    #[argh(option)]
    pub outdir: PathBuf,

    /// the directory to write generated intermediate files to.
    #[argh(option)]
    pub gendir: Option<PathBuf>,
}

/// construct an UpdatePackage using images and package.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "create-update")]
pub struct CreateUpdateArgs {
    /// path to a packages manifest, which specifies what packages to update.
    #[argh(option)]
    pub packages: Option<PathBuf>,

    /// path to a partitions config, which specifies where in the partition
    /// table the images are put.
    #[argh(option)]
    pub partitions: PathBuf,

    /// path to an images manifest, which specifies images to put in slot A.
    #[argh(option)]
    pub system_a: Option<PathBuf>,

    /// path to an images manifest, which specifies images to put in slot R.
    #[argh(option)]
    pub system_r: Option<PathBuf>,

    /// name of the board.
    /// Fuchsia will reject an Update Package with a different board name.
    #[argh(option)]
    pub board_name: String,

    /// file containing the version of the Fuchsia system.
    #[argh(option)]
    pub version_file: PathBuf,

    /// backstop OTA version.
    /// Fuchsia will reject updates with a lower epoch.
    #[argh(option)]
    pub epoch: u64,

    /// name to give the Update Package.
    /// This is currently only used by OTA tests to allow publishing multiple
    /// update packages to the same amber repository without naming collisions.
    #[argh(option)]
    pub update_package_name: Option<String>,

    /// directory to write the UpdatePackage.
    #[argh(option)]
    pub outdir: PathBuf,

    /// directory to write intermediate files.
    #[argh(option)]
    pub gendir: Option<PathBuf>,
}

/// Arguments for creating a new config data package based off an existing one.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "config_data")]
pub struct ConfigDataArgs {
    /// the filename to write the new config data meta.far to.
    /// if it is a directory, meta.far will be written to the directory.
    #[argh(option)]
    pub out_path: PathBuf,

    /// the input config data package, in the form of a meta.far.
    #[argh(option)]
    pub meta_far: PathBuf,

    /// changes to the config data package, in JSON format.
    #[argh(positional, from_str_fn(config_data_change))]
    pub changes: Vec<ConfigDataChange>,
}

/// measure package sizes and verify they fit in the specified budgets.
/// Exit status is 2 when one or more budgets are exceeded, and 1 when
/// a failure prevented the budget verification to happen.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "size-check")]
pub struct SizeCheckArgs {
    /// path to a JSON file containing the list of size budgets.
    /// Each size budget has a `name`, a `size` which is the maximum
    /// number of bytes, and `packages` a list of path to manifest files.
    #[argh(option)]
    pub budgets: PathBuf,
    /// path to a `blobs.json` file. It provides the size of each blob
    /// composing the package on device.
    #[argh(option)]
    pub blob_sizes: Vec<PathBuf>,
    /// path where to write the verification report, in JSON format.
    #[argh(option)]
    pub gerrit_output: Option<PathBuf>,
}

/// Represents a single addition or modification of a config-data file.
#[derive(Debug, Deserialize, PartialEq)]
pub struct ConfigDataChange {
    /// package for which the addition or modification should be added.
    pub package: String,

    /// path to the file to be included in the config-data package
    pub file: PathBuf,

    /// path relative to the package to put the config-data file
    pub destination: PathBuf,
}

fn config_data_change(value: &str) -> Result<ConfigDataChange, String> {
    let deserialized: Result<ConfigDataChange, serde_json::Error> = serde_json::from_str(&value);

    match deserialized {
        Ok(change) => Ok(change),
        Err(e) => Err(e.to_string()),
    }
}

/// Arguments for performing a high-level product assembly operation.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "product")]
pub struct ProductArgs {
    /// the configuration file that describes the product assembly to perform.
    #[argh(option)]
    pub product: PathBuf,

    /// the directory to write assembled outputs to.
    #[argh(option)]
    pub outdir: PathBuf,

    /// the directory to write generated intermediate files to.
    #[argh(option)]
    pub gendir: Option<PathBuf>,

    /// the directory in which to find the platform assembly input bundles
    #[argh(option)]
    pub input_bundles_dir: PathBuf,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_config_data_change() {
        let correct_change =
            "{\"package\":\"package_name\",\"file\":\"src/path\",\"destination\":\"dest/file\"}";
        let incorrect_change = "adasfasdfsa";

        let success = config_data_change(&correct_change).unwrap();

        assert_eq!(success.package, "package_name");
        assert_eq!(success.file, PathBuf::from("src/path"));
        assert_eq!(success.destination, PathBuf::from("dest/file"));

        let fail = config_data_change(incorrect_change);
        assert!(fail.is_err());
    }
}
