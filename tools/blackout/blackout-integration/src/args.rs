// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[ffx_core::ffx_command()]
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "integration", description = "Run a blackout e2e integration test")]
pub struct BlackoutIntegrationCommand {
    /// ghost arg, doesn't do anything. There doesn't seem to be a way to have a subcommand with no
    /// options.
    #[argh(switch)]
    ghost: bool,
}
