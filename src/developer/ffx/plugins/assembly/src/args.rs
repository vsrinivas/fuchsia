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
    ConfigData(ConfigDataArgs),
}

/// perform the assembly of images
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "image")]
pub struct ImageArgs {
    /// the configuration file that specifies the packages, binaries, and
    /// settings specific to the product being assembled.
    #[argh(option)]
    pub product: PathBuf,

    /// the configuration file that specifies the packages, binaries, and
    /// settings specific to the board being assembled.
    #[argh(option)]
    pub board: PathBuf,

    /// the directory to write assembled outputs to.
    #[argh(option)]
    pub outdir: PathBuf,

    /// the directory to write generated intermediate files to.
    #[argh(option)]
    pub gendir: Option<PathBuf>,

    /// run all assembly steps, even though they haven't yet been fully integrated.
    /// This is a temporary argument.
    #[argh(switch)]
    pub full: bool,
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
