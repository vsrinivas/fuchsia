// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, std::path::PathBuf};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "descriptor",
    description = "Prints the descriptors of all input devices",
    example = "Print the descriptors of all input devices:

    $ driver print-input-report descriptor",
    example = "Print the descriptor of input device 001:

    $ driver print-input-report descriptor class/input-report/001"
)]
pub struct DescriptorCommand {
    /// print the descriptor of only the input device specified by this device
    /// path which is relative to the /dev directory.
    #[argh(positional)]
    pub device_path: Option<PathBuf>,
}
