// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "doctor",
    description = "Run common checks for the ffx tool and host environment"
)]
pub struct DoctorCommand {
    #[argh(
        option,
        default = "3",
        description = "number of times to retry failed connection attempts."
    )]
    pub retry_count: usize,

    #[argh(
        option,
        default = "2000",
        description = "timeout delay when attempting to connect to the daemon or RCS"
    )]
    pub retry_delay: u64,

    #[argh(
        switch,
        description = "if true, forces a daemon restart, even if the connection appears to be working"
    )]
    pub force_daemon_restart: bool,

    #[argh(
        switch,
        description = "if true, generates an output zip file that can be attached to a monorail issue"
    )]
    pub record: bool,

    #[argh(
        option,
        description = "sets the output directory for doctor records. Only valid when --record is provided. Defaults to the current directory"
    )]
    pub record_output: Option<String>,
}
