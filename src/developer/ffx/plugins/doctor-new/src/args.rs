// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "doctor-new",
    description = "Run common checks for the ffx tool and host environment",
    example = "To run diagnostics:

    $ ffx doctor-new

To capture the output and additional logs:

    $ ffx doctor-new --record

By default, this outputs the zip in the current directory.

To override output dir:

    $ ffx doctor-new --record --output-dir /tmp/ffx",
    note = "The `doctor-new` subcommand automatically attempts to repair common target
interaction issues and provides useful diagnostic information to the user.

By default, running `ffx doctor-new` attempts to establish a connection with
the daemon, and restarts the daemon if there is no connection. The default
`retry_count` is '3' and the default 'retry_delay` is '2000' milliseconds."
)]
pub struct DoctorCommand {
    #[argh(switch, description = "generates an output zip file with logs")]
    pub record: bool,

    #[argh(switch, description = "do not include the ffx configuration file")]
    pub no_config: bool,

    #[argh(
        option,
        default = "3",
        description = "number of times to retry failed connection attempts"
    )]
    pub retry_count: usize,

    #[argh(
        option,
        default = "2000",
        description = "timeout delay in ms during connection attempt"
    )]
    pub retry_delay: u64,

    #[argh(switch, description = "force restart the daemon, even if the connection is working")]
    pub restart_daemon: bool,

    #[argh(switch, short = 'v', description = "verbose, display all steps")]
    pub verbose: bool,

    #[argh(option, description = "override the default output directory for doctor records")]
    pub output_dir: Option<String>,
}
