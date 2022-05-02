// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    blackout_target::{CommonCommand, CommonOpts},
    fuchsia_async as fasync,
    structopt::StructOpt,
};

#[derive(StructOpt)]
#[structopt(rename_all = "kebab-case")]
struct Opts {
    #[structopt(flatten)]
    common: CommonOpts,
    /// A particular step of the test to perform.
    #[structopt(subcommand)]
    commands: CommonCommand,
}

fn setup() -> Result<()> {
    println!("setup called");
    Ok(())
}

fn test() -> Result<()> {
    println!("test called");
    loop {}
}

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    let opts = Opts::from_args();

    println!("opts: {:?}", opts.common);

    match opts.commands {
        CommonCommand::Setup => setup()?,
        CommonCommand::Test => test()?,
        CommonCommand::Verify => {
            println!("verify called - failing");
            std::process::exit(blackout_target::VERIFICATION_FAILURE_EXIT_CODE);
        }
    }

    Ok(())
}
