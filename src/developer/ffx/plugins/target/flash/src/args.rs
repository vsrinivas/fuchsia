// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    ffx_core::ffx_command,
    ffx_fastboot::common::cmd::{Command, ManifestParams, OemFile},
    std::default::Default,
    std::path::PathBuf,
};

#[ffx_command()]
#[derive(FromArgs, Default, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "flash",
    description = "Flash an image to a target device",
    example = "To flash a specific image:

    $ ffx target flash ~/fuchsia/out/flash.json fuchsia

To include SSH keys as well:

    $ ffx target flash
    --ssh-key ~/fuchsia/.ssh/authorized_keys
    ~/fuchsia/out/default/flash.json
    --product fuchsia",
    note = "Flashes an image to a target device using the fastboot protocol.
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
    #[argh(
        positional,
        description = "path to flashing manifest or zip file containing images and manifest"
    )]
    pub manifest: Option<PathBuf>,

    #[argh(
        option,
        short = 'p',
        description = "product entry in manifest - defaults to `fuchsia`",
        default = "String::from(\"fuchsia\")"
    )]
    pub product: String,

    #[argh(option, short = 'b', description = "optional product bundle name")]
    pub product_bundle: Option<String>,

    #[argh(option, description = "oem staged file - can be supplied multiple times")]
    pub oem_stage: Vec<OemFile>,

    #[argh(
        option,
        description = "path to ssh key - will default to the `ssh.pub` \
           key in ffx config"
    )]
    pub ssh_key: Option<String>,

    #[argh(
        switch,
        description = "the device should not reboot after bootloader images are flashed"
    )]
    pub no_bootloader_reboot: bool,

    #[argh(
        switch,
        description = "skip hardware verification. This is dangerous, please be sure the images you are flashing match the device"
    )]
    pub skip_verify: bool,
}

impl Into<ManifestParams> for FlashCommand {
    fn into(self) -> ManifestParams {
        ManifestParams {
            manifest: self.manifest,
            product: self.product,
            product_bundle: self.product_bundle,
            oem_stage: self.oem_stage,
            skip_verify: self.skip_verify,
            no_bootloader_reboot: self.no_bootloader_reboot,
            op: Command::Flash,
            ..Default::default()
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use anyhow::Result;
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

    #[test]
    fn test_oem_staged_files_are_in_manifest_params() -> Result<()> {
        let test_oem_cmd = "test-oem-cmd";
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let test_staged_file = format!("{},{}", test_oem_cmd, tmp_file_name).parse::<OemFile>()?;
        let cmd = FlashCommand {
            manifest: None,
            product: "fuchsia".to_string(),
            product_bundle: None,
            ssh_key: None,
            no_bootloader_reboot: false,
            skip_verify: false,
            oem_stage: vec![test_staged_file],
        };

        let params: ManifestParams = cmd.into();
        assert_eq!(params.oem_stage[0].file(), tmp_file_name);
        assert_eq!(params.oem_stage[0].command(), test_oem_cmd);
        Ok(())
    }
}
