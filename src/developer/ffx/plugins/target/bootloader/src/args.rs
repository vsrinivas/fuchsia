// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    ffx_core::ffx_command,
    ffx_fastboot::common::cmd::{BootParams, Command, ManifestParams, UnlockParams},
    std::path::PathBuf,
};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "bootloader", description = "Communicates with the bootloader")]
pub struct BootloaderCommand {
    #[argh(
        option,
        short = 'm',
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

    #[argh(
        switch,
        description = "skip hardware verification.  This is dangerous, please be sure the images you are using match the device"
    )]
    pub skip_verify: bool,

    #[argh(subcommand)]
    pub subcommand: Subcommand,
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
#[argh(subcommand)]
pub enum Subcommand {
    Lock(LockCommand),
    Unlock(UnlockCommand),
    Boot(BootCommand),
    Info(InfoCommand),
}

#[derive(FromArgs, Default, Clone, PartialEq, Debug)]
/// Locks a fastboot target.
#[argh(subcommand, name = "lock")]
pub struct LockCommand {}

#[derive(FromArgs, Default, Clone, PartialEq, Debug)]
/// Prints fastboot variables for target.
#[argh(subcommand, name = "info")]
pub struct InfoCommand {}

#[derive(FromArgs, Default, Clone, PartialEq, Debug)]
/// Unlocks a fastboot target.
#[argh(subcommand, name = "unlock")]
pub struct UnlockCommand {
    #[argh(
        option,
        short = 'c',
        description = "optional path to credential file to use to unlock the device"
    )]
    pub cred: Option<String>,

    #[argh(switch, description = "skips the warning message that this command is dangerous")]
    pub force: bool,
}

#[derive(FromArgs, Default, Clone, PartialEq, Debug)]
/// RAM boots a fastboot target.
#[argh(subcommand, name = "boot")]
pub struct BootCommand {
    #[argh(option, short = 'z', description = "optional zbi image file path to use")]
    pub zbi: Option<String>,

    #[argh(option, short = 'v', description = "optional vbmeta image file path to use")]
    pub vbmeta: Option<String>,

    #[argh(
        option,
        short = 's',
        description = "slot corresponding to partitions in the flash manifest - \n\
        only used if zbi and vbmeta files are not present",
        default = "String::from(\"a\")"
    )]
    pub slot: String,
}

impl Into<ManifestParams> for BootloaderCommand {
    fn into(self) -> ManifestParams {
        let op = match self.subcommand {
            Subcommand::Boot(BootCommand { zbi, vbmeta, slot }) => {
                Command::Boot(BootParams { zbi, vbmeta, slot })
            }
            Subcommand::Unlock(UnlockCommand { cred, force }) => {
                Command::Unlock(UnlockParams { cred, force })
            }
            _ => panic!(), // Should never get here
        };
        ManifestParams {
            manifest: self.manifest,
            product: self.product,
            product_bundle: self.product_bundle,
            skip_verify: self.skip_verify,
            op,
            ..Default::default()
        }
    }
}
