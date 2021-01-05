// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
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
}
