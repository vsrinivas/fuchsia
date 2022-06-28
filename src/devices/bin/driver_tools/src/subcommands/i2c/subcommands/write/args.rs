// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, std::path::PathBuf};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "write",
    description = "Writes data to an I2C device",
    example = "Write the bytes 0xab and 0x01 to I2C device 001:

    $ driver i2cutil write class/i2c/001 171 1"
)]
pub struct WriteCommand {
    #[argh(positional, description = "path of the I2C device relative to devfs")]
    pub device_path: PathBuf,

    #[argh(
        positional,
        description = "spaces-separated bytes, in decimal format, to be written to the I2C device"
    )]
    pub data: Vec<u8>,
}
