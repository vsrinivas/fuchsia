// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Error, Result},
    argh::FromArgs,
    ffx_core::ffx_command,
    serde::{Deserialize, Serialize},
    std::path::Path,
};

pub(crate) const OEM_FILE_ERROR_MSG: &str =
    "Unrecognized OEM staged file. Expected comma-seperated pair: \"<OEM_COMMAND>,<PATH_TO_FILE>\"";

#[ffx_command()]
#[derive(FromArgs, Default, Debug, PartialEq, Clone)]
#[argh(
    subcommand,
    name = "flash",
    description = "Flash an image to a target device",
    example = "To flash a specific image:

    $ ffx target flash ~/fuchsia/out/flash.json fuchsia

To include SSH keys as well:

    $ ffx target flash
    --oem-stage add-staged-bootloader-file ssh.authorized_keys,
    ~/fuchsia/.ssh/authorized_keys
    ~/fuchsia/out/flash.json
     fuchsia",
    note = "Flases an image to a target device using the fastboot protocol.
Requires a specific <manifest> file and <product> name as an input.

This is only applicable to a physical device and not an emulator target.
The target device is typically connected via a micro-USB connection to
the host system.

The <manifest> format is a JSON file generated when building a fuchsia
<product> and can be found in the build output directory.

The `--oem-stage` option can be supplied multiple times for several OEM
files. The format expects a single OEM command to execute after staging
the given file.

The format for the `--oem-stage` parameter is a comma separated pair:
'<OEM_COMMAND>,<FILE_TO_STAGE>'"
)]
pub struct FlashCommand {
    //path to flashing manifest
    #[argh(positional)]
    pub manifest: String,

    //product to flash
    #[argh(positional)]
    pub product: Option<String>,

    #[argh(option, description = "oem staged file - can be supplied multiple times")]
    pub oem_stage: Vec<OemFile>,
}

#[derive(Default, Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct OemFile(String, String);

impl OemFile {
    pub fn command(&self) -> &str {
        self.0.as_str()
    }

    pub fn file(&self) -> &str {
        self.1.as_str()
    }
}

impl std::str::FromStr for OemFile {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self> {
        if s.len() == 0 {
            bail!(OEM_FILE_ERROR_MSG);
        }

        let splits: Vec<&str> = s.split(",").collect();

        if splits.len() != 2 {
            bail!(OEM_FILE_ERROR_MSG);
        }

        let file = Path::new(splits[1]);
        if !file.exists() {
            bail!("File does not exist: {}", splits[1]);
        }

        Ok(Self(splits[0].to_string(), file.to_string_lossy().to_string()))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use tempfile::NamedTempFile;

    #[test]
    fn test_oem_staged_file_from_str() -> Result<()> {
        let test_oem_cmd = "test-oem-cmd";
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let test_staged_file = format!("{},{}", test_oem_cmd, tmp_file_name).parse::<OemFile>()?;
        assert_eq!(test_oem_cmd, test_staged_file.command());
        assert_eq!(tmp_file_name, test_staged_file.file());
        Ok(())
    }

    #[test]
    fn test_oem_staged_file_from_str_fails_with_nonexistent_file() {
        let test_oem_cmd = "test-oem-cmd";
        let tmp_file_name = "/fake/test/for/testing/that/should/not/exist";
        let test_staged_file = format!("{},{}", test_oem_cmd, tmp_file_name).parse::<OemFile>();
        assert!(test_staged_file.is_err());
    }

    #[test]
    fn test_oem_staged_file_from_str_fails_with_malformed_string() {
        let test_oem_cmd = "test-oem-cmd";
        let tmp_file_name = "/fake/test/for/testing/that/should/not/exist";
        let test_staged_file = format!("{}..{}", test_oem_cmd, tmp_file_name).parse::<OemFile>();
        assert!(test_staged_file.is_err());
    }

    #[test]
    fn test_oem_staged_file_from_str_fails_with_empty_string() {
        let test_staged_file = "".parse::<OemFile>();
        assert!(test_staged_file.is_err());
    }
}
