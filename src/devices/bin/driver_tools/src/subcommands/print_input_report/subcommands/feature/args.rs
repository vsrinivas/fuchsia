// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, std::path::PathBuf};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "feature",
    description = "Prints the features of a given input device",
    example = "Print the features of input device 001:

    $ driver print-input-report feature class/input-report/001"
)]
pub struct FeatureCommand {
    /// print the features of only the input device specified by this device
    /// path which is relative to the /dev directory.
    #[argh(positional)]
    pub device_path: PathBuf,
}
