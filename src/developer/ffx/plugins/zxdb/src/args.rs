// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "debug",
    description = "[EXPERIMENTAL] Start a debugging session. \
    Currently only runs inside the Fuchsia tree."
)]
pub struct DebugCommand {
    #[argh(positional, default = "String::from(\"/tmp/zxdb.socket\")")]
    pub socket_location: String,
}
