// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, std::path::PathBuf};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "read",
    description = "Prints the input reports of all input devices in real-time",
    example = "Print the input reports of all input devices in real-time:

    $ driver print-input-report read"
)]
pub struct ReadCommand {
    /// print the input reports of only the specified input device.
    #[argh(subcommand)]
    pub devpath: Option<Devpath>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "devpath",
    description = "Prints the input reports of only a specified input device",
    example = "Print the next 5 input reports of the input device 001:

    $ driver print-input-report read class/input-report/001 --num-reads 5"
)]
pub struct Devpath {
    /// device path of the device, relative to the /dev directory.
    #[argh(positional)]
    pub device_path: PathBuf,

    /// how many input reports to read.
    #[argh(option, default = "usize::MAX")]
    pub num_reads: usize,
}
