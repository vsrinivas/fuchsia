// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate fidl;
extern crate failure;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate garnet_examples_fidl2_services_echo2;
#[macro_use]
extern crate structopt;

use app::client::Launcher;
use failure::{Error, ResultExt};
use futures::prelude::*;
use garnet_examples_fidl2_services_echo2::EchoProxy;
use structopt::StructOpt;

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
}

fn main_res() -> Result<(), Error> {
    let mut executor = async::Executor::new().context("Error creating executor")?;

    #[derive(StructOpt, Debug)]
    #[structopt(name = "echo_client_rust")]
    struct Opt {
        #[structopt(long = "server", help = "URL of echo server",
                    default_value = "echo2_server_rust")]
        server_url: String,
    }

    // Launch the server and connect to the echo service.
    let Opt { server_url } = Opt::from_args();

    let launcher = Launcher::new().context("Failed to open launcher service")?;
    let app = launcher.launch(server_url, None)
                      .context("Failed to launch echo service")?;

    let (client_end, server_end) = zx::Channel::create().context("Failed to create channel")?;

    app.connect_to_service_raw(server_end, "echo2.Echo")
       .context("Failed to connect to echo service")?;

    let echo = EchoProxy::new(
        async::Channel::from_channel(client_end).context("failed to make async channel")?);

    let fut = echo.echo_string(&mut "hello world!".to_string())
        .map(|res| println!("response: {:?}", res));

    executor.run_singlethreaded(fut).context("failed to execute echo future")?;
    Ok(())
}
