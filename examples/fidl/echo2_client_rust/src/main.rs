// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use failure::{Error, ResultExt};
use fidl_fidl_examples_echo::EchoMarker;
use fuchsia_app::client::Launcher;
use fuchsia_async as fasync;
use structopt::StructOpt;

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    #[derive(StructOpt, Debug)]
    #[structopt(name = "echo_client_rust")]
    struct Opt {
        #[structopt(long = "server", help = "URL of echo server",
                    default_value = "fuchsia-pkg://fuchsia.com/echo2_server_rust#meta/echo2_server_rust.cmx")]
        server_url: String,
    }

    // Launch the server and connect to the echo service.
    let Opt { server_url } = Opt::from_args();

    let launcher = Launcher::new().context("Failed to open launcher service")?;
    let app = launcher.launch(server_url, None)
                      .context("Failed to launch echo service")?;

    let echo = app.connect_to_service(EchoMarker)
       .context("Failed to connect to echo service")?;

    let fut = async {
        let res = await!(echo.echo_string(Some("hello world!")))?;
        println!("response: {:?}", res);
        Ok(())
    };

    executor.run_singlethreaded(fut)
}
