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
    LogStats(log_stats::CommandLine),
    Lapis(sampler::Args),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let log_tag = match std::env::args().nth(1).as_ref().map(|s| s.as_str()) {
        Some(log_stats::PROGRAM_NAME) => log_stats::PROGRAM_NAME,
        Some(detect::PROGRAM_NAME) => detect::PROGRAM_NAME,
        Some(sampler::PROGRAM_NAME) => sampler::PROGRAM_NAME,
        // If the name is invalid, don't quit yet - give argh a chance to log
        // help text. Then the program will exit.
        _ => "launcher",
    };
    fuchsia_syslog::init_with_tags(&[log_tag]).context("initializing logging").unwrap();
    let args = v2_argh_wrapper::load_command_line::<LauncherArgs>()?;
    match args.program {
        ChildArgs::Detect(args) => detect::main(args).await,
        ChildArgs::LogStats(_args) => log_stats::main().await,
        ChildArgs::Lapis(args) => sampler::main(args).await,
    }
}
