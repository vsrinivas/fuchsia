// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, std::path::PathBuf};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "transact",
    description = "Sends a sequence of I2C transactions to an I2C device in the order they are written",
    example = "Send transactions to read 100 bytes, then write the bytes 0xab, 0x02, and 0xff; and then read 4 bytes to I2C device 004:

    $ driver i2cutil transact r 100 w 171 2 255 r 4"
)]
pub struct TransactCommand {
    #[argh(positional, description = "path of the I2C device relative to devfs")]
    pub device_path: PathBuf,

    #[argh(positional, description = "transactions to send to I2C device")]
    pub transactions: Vec<String>,
}
