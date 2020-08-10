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
}
