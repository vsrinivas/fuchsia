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
}
