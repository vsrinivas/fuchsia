// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `launcher` launches librarified subprograms. See README.md.

use {anyhow::Error, argh::FromArgs};

/// Top-level command.
#[derive(FromArgs, PartialEq, Debug)]
struct LauncherArgs {
    #[argh(subcommand)]
    program: CreateChildArgs,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum CreateChildArgs {
    Detect(detect::CommandLine),
    LogStats(log_stats::CommandLine),
    Sampler(sampler::Args),
    Persistence(persistence::CommandLine),
    Kcounter(kcounter::CommandLine),
}

#[fuchsia::component(logging = false)]
async fn main() -> Result<(), Error> {
    let log_tag = match std::env::args().nth(1).as_ref().map(|s| s.as_str()) {
        Some(log_stats::PROGRAM_NAME) => log_stats::PROGRAM_NAME,
        Some(detect::PROGRAM_NAME) => detect::PROGRAM_NAME,
        Some(sampler::PROGRAM_NAME) => sampler::PROGRAM_NAME,
        Some(persistence::PROGRAM_NAME) => persistence::PROGRAM_NAME,
        Some(kcounter::PROGRAM_NAME) => kcounter::PROGRAM_NAME,
        // If the name is invalid, don't quit yet - give argh a chance to log
        // help text. Then the program will exit.
        _ => "launcher",
    };
    diagnostics_log::init!(&[log_tag]);
    let args = v2_argh_wrapper::load_command_line::<LauncherArgs>()?;
    match args.program {
        CreateChildArgs::Detect(args) => detect::main(args).await,
        CreateChildArgs::Persistence(args) => persistence::main(args).await,
        CreateChildArgs::LogStats(_args) => log_stats::main().await,
        CreateChildArgs::Sampler(args) => sampler::main(args).await,
        CreateChildArgs::Kcounter(_args) => kcounter::main().await,
    }
}
