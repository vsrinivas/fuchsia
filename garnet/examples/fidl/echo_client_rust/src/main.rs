// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use failure::{Error, ResultExt};
use fidl_fidl_examples_echo::EchoMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::{launcher, launch};
use structopt::StructOpt;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    #[derive(StructOpt, Debug)]
    #[structopt(name = "echo_client_rust")]
    struct Opt {
        #[structopt(long = "server", help = "URL of echo server",
                    default_value = "fuchsia-pkg://fuchsia.com/echo_server_rust#meta/echo_server_rust.cmx")]
        server_url: String,
    }

    // Launch the server and connect to the echo service.
    let Opt { server_url } = Opt::from_args();

    let launcher = launcher().context("Failed to open launcher service")?;
    let app = launch(&launcher, server_url, None)
                      .context("Failed to launch echo service")?;

    let echo = app.connect_to_service::<EchoMarker>()
       .context("Failed to connect to echo service")?;

    let res = await!(echo.echo_string(Some("hello world!")))?;
    println!("response: {:?}", res);
    Ok(())
}
