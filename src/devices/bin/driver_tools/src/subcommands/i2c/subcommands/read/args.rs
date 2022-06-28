// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, std::path::PathBuf};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "read",
    description = "Reads a single byte from the address of an I2C device",
    example = "Read a single byte from the address 0xff04 of the I2C device 000:

    $ driver i2cutil read class/i2c/000 255 4"
)]
pub struct ReadCommand {
    #[argh(positional, description = "path of the I2C device relative to devfs")]
    pub device_path: PathBuf,

    #[argh(
        positional,
        description = "space-separated bytes, in decimal format, that represent the address of the byte to read"
    )]
    pub address: Vec<u8>,
}
