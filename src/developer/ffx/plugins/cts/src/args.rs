// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "cts", description = "Build and run CTS tests.")]
pub struct CtsCommand {
    #[argh(subcommand)]
    pub command: Args,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum Args {
    Run(RunCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "run", description = "Run CTS tests.")]
pub struct RunCommand {
    /// string, version of CTS to run.
    #[argh(option)]
    pub cts_version: Option<String>,

    /// string, comma-separated list of tests to run.
    #[argh(option)]
    pub tests: Option<String>,
}
