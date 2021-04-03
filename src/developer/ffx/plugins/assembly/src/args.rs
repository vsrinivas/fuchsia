// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "assembly", description = "Assemble images")]
pub struct AssemblyCommand {
    /// a value to assemble into a message
    #[argh(option)]
    pub value: String,
}
