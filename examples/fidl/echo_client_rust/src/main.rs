// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
#[macro_use]
extern crate structopt;
use structopt::StructOpt;

// This is the generated crate containing FIDL bindings for the Echo service.
extern crate garnet_examples_fidl_services;
use garnet_examples_fidl_services::Echo;

// The `failure` crate provides an error trait called `Fail`,
// as well as a standard dynamically-dispatched `Error` type.
// The `ResultExt` trait adds the `context` method to errors, which allows
// us to add additional information to errors as they bubble up the stack.
use failure::{Error, ResultExt};

// The fuchsia_app crate allows us to start up applications.
use component::client::Launcher;

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
}

/// Starts an echo server, sends a message, and prints the response.
fn main_res() -> Result<(), Error> {
    let mut executor = async::Executor::new().context("Error creating executor")?;

    let launcher = Launcher::new().context("Failed to open launcher service")?;

    #[derive(StructOpt, Debug)]
    #[structopt(name = "echo_client_rust")]
    struct Opt {
        #[structopt(long = "server", help = "URL of echo server",
                    default_value = "echo_server_rust")]
        server_url: String,
    }

    // Launch the server and connect to the echo service.
    let Opt { server_url } = Opt::from_args();

    let app = launcher.launch(server_url, None)
                      .context("Failed to launch echo service")?;

    let echo = app.connect_to_service::<Echo::Service>()
                  .context("Failed to connect to echo service")?;

    // Send "echo msg" to the server.
    // `response_fut` is a `Future` which returns `Option<String>`.
    let response_fut = echo.echo_string(Some(String::from("echo msg")));

    // Run `response_fut` to completion using 2 worker threads.
    let response = executor.run(response_fut, 2).context("Error getting echo response")?;
    println!("{:?}", response);

    Ok(())
}
