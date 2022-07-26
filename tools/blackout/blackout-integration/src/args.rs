// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[ffx_core::ffx_command()]
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "integration", description = "Run a blackout e2e integration test")]
pub struct BlackoutIntegrationCommand {
    /// run the test N number of times, collecting statistics on the number of failures.
    #[argh(option, short = 'i', long = "iterations")]
    pub iterations: Option<u64>,
    /// configure the tests to add a reboot step between the load and verification steps.
    #[argh(switch)]
    pub reboot: bool,
    /// run a bootserver in the background for serving netboot images. This only works when the
    /// test is run by botanist, as the environment variables and bootserver arguments are specific
    /// to that environment.
    #[argh(switch)]
    pub bootserver: bool,
}
