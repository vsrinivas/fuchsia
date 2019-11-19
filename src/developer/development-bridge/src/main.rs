// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, failure::Error};

#[derive(FromArgs)]
/// Fuchsia Development Bridge
struct Fdb {
    #[argh(subcommand)]
    subcommand: Subcommand,
}

#[derive(FromArgs)]
#[argh(subcommand, name = "start", description = "start")]
struct StartCommand {}

#[derive(FromArgs)]
#[argh(subcommand)]
enum Subcommand {
    Start(StartCommand),
}

async fn exec_start() -> Result<(), Error> {
    log::info!("Starting...");
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// main

async fn async_main() -> Result<(), Error> {
    let app: Fdb = argh::from_env();

    match app.subcommand {
        Subcommand::Start(_) => exec_start().await,
    }
}

fn main() {
    hoist::run(async move {
        async_main().await.map_err(|e| log::error!("{}", e)).expect("could not start fdb");
    })
}
