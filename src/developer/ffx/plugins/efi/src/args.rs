// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "efi", description = "Manipulate efi partition")]
pub struct EfiCommand {
    #[argh(subcommand)]
    pub subcommand: EfiSubcommand,
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
#[argh(subcommand)]
pub enum EfiSubcommand {
    Create(CreateCommand),
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
/// Creates efi partition, copies zircon.bin, bootdata.bin, EFI/BOOT/BOOTX64.EFI, zedboot.bin,
/// etc...
#[argh(subcommand, name = "create")]
pub struct CreateCommand {
    #[argh(option, short = 'o')]
    /// target file/disk to write EFI partition to
    pub output: String,
    /// optional path to source file for zircon.bin
    #[argh(option)]
    pub zircon: Option<String>,
    /// optional path to source file for bootdata.bin
    #[argh(option)]
    pub bootdata: Option<String>,
    // TODO(vol): name doesn't match make-efi command argument
    /// optional path to source file for EFI/BOOT/BOOTX64.EFI
    #[argh(option)]
    pub efi_bootloader: Option<String>,
    /// optional path to a source file for zedboot.bin
    #[argh(option)]
    pub zedboot: Option<String>,
    /// optional bootloader cmdline file
    #[argh(option)]
    pub cmdline: Option<String>,
}
