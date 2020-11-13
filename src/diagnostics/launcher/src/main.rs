// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `launcher` launches librarified subprograms. See README.md.

use {
    anyhow::{Context, Error},
    argh::FromArgs,
    fuchsia_async as fasync,
};

#[derive(FromArgs, PartialEq, Debug)]
/// Top-level command.
struct LauncherArgs {
    #[argh(subcommand)]
    program: ChildArgs,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum ChildArgs {
    Detect(detect::CommandLine),
    Lapis(sampler::Args),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().context("initializing logging").unwrap();
    let args = v2_argh_wrapper::load_command_line::<LauncherArgs>()?;
    match args.program {
        ChildArgs::Detect(args) => detect::main(args).await,
        ChildArgs::Lapis(args) => sampler::main(args).await,
    }
}
