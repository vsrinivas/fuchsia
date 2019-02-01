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
     fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker, ServerEnd},
     fidl_fidl_examples_echo::{EchoMarker, EchoRequest},
     fidl_fuchsia_overnet_examples_interfacepassing::{ExampleMarker, ExampleRequest, ExampleRequestStream},
     fidl_fuchsia_overnet::{OvernetMarker, OvernetProxy, ServiceProviderRequest,
                            ServiceProviderRequestStream},
     fuchsia_async as fasync, fuchsia_zircon as zx,
     futures::prelude::*};

fn app<'a, 'b>() -> App<'a, 'b> {
    App::new("echo-server")
        .version("0.1.0")
        .about("Echo server example for overnet")
        .author("Fuchsia Team")
        .arg(Arg::with_name("quiet").help("Should output be quiet"))
}

fn spawn_echo_server(chan: ServerEnd<EchoMarker>, quiet: bool) {
    fasync::spawn(
        async move {
            let mut stream = chan.into_stream()?;
            while let Some(EchoRequest::EchoString { value, responder }) =
                await!(stream.try_next()).context("error running echo server")?
            {
                if !quiet {
                    println!("Received echo request for string {:?}", value);
                }
                responder
                    .send(value.as_ref().map(|s| &**s))
                    .context("error sending response")?;
                if !quiet {
                    println!("echo response sent successfully");
                }
            }
            Ok(())
        }
            .unwrap_or_else(|e: failure::Error| eprintln!("{:?}", e)),
    );
}

fn spawn_example_server(chan: fasync::Channel, quiet: bool) {
    fasync::spawn(
        async move {
            let mut stream = ExampleRequestStream::from_channel(chan);
            while let Some(request) =
                await!(stream.try_next()).context("error running echo server")?
            {
                match request {
                    ExampleRequest::Request { iface, .. } => {
                        if !quiet {
                            println!("Received interface request");
                        }
                        spawn_echo_server(iface, quiet);
                    }
                }
            }
            Ok(())
        }
            .unwrap_or_else(|e: failure::Error| eprintln!("{:?}", e)),
    );
}

async fn next_request(stream: &mut ServiceProviderRequestStream) -> Result<Option<ServiceProviderRequest>, Error> {
    println!("Awaiting request");
    Ok(await!(stream.try_next()).context("error running service provider server")?)
}

async fn exec(svc: OvernetProxy, quiet: bool) -> Result<(), Error> {
    let (s, p) = zx::Channel::create().context("failed to create zx channel")?;
    let chan = fasync::Channel::from_channel(s).context("failed to make async channel")?;
    let mut stream = ServiceProviderRequestStream::from_channel(chan);
    svc.register_service(ExampleMarker::NAME, ClientEnd::new(p))?;
    while let Some(ServiceProviderRequest::ConnectToService {
        service_name,
        chan,
        control_handle: _control_handle,
    }) = await!(next_request(&mut stream))?
    {
        if !quiet {
            println!("Received service request for service {:?}", service_name);
        }
        let chan = fasync::Channel::from_channel(chan).context("failed to make async channel")?;
        spawn_example_server(chan, quiet);
    }
    Ok(())
}

fn main() -> Result<(), Error> {
    let args = app().get_matches();

    let mut executor = fasync::Executor::new().context("error creating event loop")?;
    let svc = fuchsia_app::client::connect_to_service::<OvernetMarker>()
        .context("Failed to connect to overnet service")?;

    executor
        .run_singlethreaded(exec(svc, args.is_present("quiet")))
        .map_err(Into::into)
}
