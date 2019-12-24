// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use argh::FromArgs;
use fidl_fidl_examples_echo::EchoServiceMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, launcher};

// [START main]
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    #[allow(dead_code)] // FIXME(cramertj) this shouldn't be required
    #[derive(FromArgs, Debug)]
    /// Rust echo client
    struct Opt {
        /// URL of the echo server to run.
        #[argh(
            option,
            long = "server",
            default = "\"fuchsia-pkg://fuchsia.com/echo_server_rust#meta/echo_server_rust.cmx\"\
                       .to_string()"
        )]
        server_url: String,
    }

    // Launch the server and connect to the echo service.
    let Opt { server_url } = argh::from_env();

    let launcher = launcher().context("Failed to open launcher service")?;
    let app = launch(&launcher, server_url, None).context("Failed to launch echo service")?;

    let echo = app
        .connect_to_unified_service::<EchoServiceMarker>()
        .context("Failed to connect to echo service")?;

    let foo = echo.foo().context("failed to connect to foo member")?;

    let res = foo.echo_string(Some("hello world!")).await?;
    println!("response: {:?}", res);
    Ok(())
}
// [END main]
