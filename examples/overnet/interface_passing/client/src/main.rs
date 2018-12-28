// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(
    async_await,
    await_macro,
    futures_api,
)]

use {clap::{App, Arg},
     failure::{Error, ResultExt},
     fidl::endpoints::{ServerEnd, ServiceMarker},
     fidl_fidl_examples_echo as echo,
     fidl_fuchsia_overnet_examples_interfacepassing as interfacepassing,
     fidl_fuchsia_overnet::{OvernetMarker, OvernetProxy},
     fuchsia_async as fasync, fuchsia_zircon as zx};

fn app<'a, 'b>() -> App<'a, 'b> {
    App::new("echo-client")
        .version("0.1.0")
        .about("Echo client example for overnet")
        .author("Fuchsia Team")
        .arg(
            Arg::with_name("text")
                .help("Text string to echo back and forth")
                .takes_value(true),
        )
}

async fn exec(svc: OvernetProxy, text: Option<&str>) -> Result<(), Error> {
    loop {
        let peers = await!(svc.list_peers())?;
        for peer in peers {
            let (s, p) = zx::Channel::create().context("failed to create zx channel")?;
            if let Err(e) =
                svc.connect_to_service(peer.id, interfacepassing::ExampleMarker::NAME, s)
            {
                println!("{:?}", e);
                continue;
            }
            let proxy = fasync::Channel::from_channel(p).context("failed to make async channel")?;
            let cli = interfacepassing::ExampleProxy::new(proxy);

            let (s1, p1) = zx::Channel::create().context("failed to create zx channel")?;
            let proxy_echo = fasync::Channel::from_channel(p1).context("failed to make async channel")?;
            let cli_echo = echo::EchoProxy::new(proxy_echo);
            println!("Sending {:?} to {}", text, peer.id);
            if let Err(e) = cli.request(ServerEnd::new(s1)) {
                println!("ERROR REQUESTING INTERFACE: {:?}", e);
                continue;
            }
            println!("received {:?}", await!(cli_echo.echo_string(text))?);
            return Ok(());
        }
    }
}

fn main() -> Result<(), Error> {
    let args = app().get_matches();

    let mut executor = fasync::Executor::new().context("error creating event loop")?;
    let svc = fuchsia_app::client::connect_to_service::<OvernetMarker>()
        .context("Failed to connect to overnet service")?;

    let text = args.value_of("text");
    executor
        .run_singlethreaded(exec(svc, text))
        .map_err(Into::into)
}
