// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fuchsia_async as fasync, step_lib::Shell, structopt::StructOpt,
    test_utils_lib::opaque_test::OpaqueTest,
};

#[derive(Clone, Debug, StructOpt)]
#[structopt(
    name = "Component manager event breakpoint tool (step)",
    about = "An interactive event breakpoint system for component manager",
    raw(setting = "structopt::clap::AppSettings::ColoredHelp")
)]
struct Args {
    /// The v2 component URL to run as the root component
    component_url: String,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Get arguments from command line
    let args = Args::from_args();

    println!("------------------ step is starting --------------------");
    println!("ARGUMENTS = {:#?}", args);
    println!("--------------------------------------------------------");

    let mut test = OpaqueTest::default(&args.component_url).await.unwrap();
    println!("Component manager has started");

    let event_source = test.connect_to_event_source().await.unwrap();
    let shell = Shell::new(event_source).await;

    // Run the shell in a new thread
    let shell_task = fasync::Task::blocking(async move {
        shell.run().await;
    });

    // Wait for component manager to exit
    let exit_status = test.component_manager_app.wait().await.unwrap();

    println!("");
    println!("---------------------------------------------");
    match exit_status.ok() {
        Ok(()) => {
            println!("Component manager exited cleanly!");
        }
        Err(status) => {
            println!("Component manager crashed! {}", status);
        }
    }
    println!("---------------------------------------------");

    shell_task.await;

    Ok(())
}
