// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::subcommands::{
        ping::args::PingCommand, read::args::ReadCommand, transact::args::TransactCommand,
        write::args::WriteCommand,
    },
    argh::FromArgs,
};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "i2c", description = "Perform reads and writes on an I2C device")]
pub struct I2cCommand {
    #[argh(subcommand)]
    pub subcommand: I2cSubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum I2cSubCommand {
    Ping(PingCommand),
    Read(ReadCommand),
    Transact(TransactCommand),
    Write(WriteCommand),
}
