// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
extern crate log;

use {
    clap::{App, Arg, SubCommand},
    failure::{Error, ResultExt},
    fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker},
    fidl_fidl_examples_echo as echo,
    fidl_fuchsia_overnet::{
        OvernetProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
    },
    futures::prelude::*,
};

fn app<'a, 'b>() -> App<'a, 'b> {
    App::new("overnet-echo")
        .version("0.1.0")
        .about("Echo example for overnet")
        .author("Fuchsia Team")
        .arg(Arg::with_name("quiet").help("Should output be quiet"))
        .subcommand(SubCommand::with_name("client").about("Run as client").arg(
            Arg::with_name("text").help("Text string to echo back and forth").takes_value(true),
        ))
        .subcommand(SubCommand::with_name("server").about("Run as server"))
}

////////////////////////////////////////////////////////////////////////////////
// Client implementation

async fn exec_client(svc: impl OvernetProxyInterface, text: Option<&str>) -> Result<(), Error> {
    loop {
        let peers = svc.list_peers().await?;
        trace!("Got peers: {:?}", peers);
        for mut peer in peers {
            if peer.description.services.is_none() {
                continue;
            }
            if peer
                .description
                .services
                .unwrap()
                .iter()
                .find(|name| *name == echo::EchoMarker::NAME)
                .is_none()
            {
                continue;
            }
            let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
            if let Err(e) = svc.connect_to_service(&mut peer.id, echo::EchoMarker::NAME, s) {
                trace!("{:?}", e);
                continue;
            }
            let proxy =
                fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
            let cli = echo::EchoProxy::new(proxy);
            trace!("Sending {:?} to {:?}", text, peer.id);
            match cli.echo_string(text).await {
                Ok(r) => {
                    trace!("SUCCESS: received {:?}", r);
                    return Ok(());
                }
                Err(e) => {
                    trace!("ERROR: {:?}", e);
                    continue;
                }
            };
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Server implementation

fn spawn_echo_server(chan: fidl::AsyncChannel, quiet: bool) {
    hoist::spawn(
        async move {
            let mut stream = echo::EchoRequestStream::from_channel(chan);
            while let Some(echo::EchoRequest::EchoString { value, responder }) =
                stream.try_next().await.context("error running echo server")?
            {
                if !quiet {
                    trace!("Received echo request for string {:?}", value);
                }
                responder.send(value.as_ref().map(|s| &**s)).context("error sending response")?;
                if !quiet {
                    trace!("echo response sent successfully");
                }
            }
            Ok(())
        }
            .unwrap_or_else(|e: failure::Error| trace!("{:?}", e)),
    );
}

async fn next_request(
    stream: &mut ServiceProviderRequestStream,
) -> Result<Option<ServiceProviderRequest>, Error> {
    trace!("Awaiting request");
    Ok(stream.try_next().await.context("error running service provider server")?)
}

async fn exec_server(svc: impl OvernetProxyInterface, quiet: bool) -> Result<(), Error> {
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
    let mut stream = ServiceProviderRequestStream::from_channel(chan);
    svc.register_service(echo::EchoMarker::NAME, ClientEnd::new(p))?;
    while let Some(ServiceProviderRequest::ConnectToService {
        chan,
        info: _,
        control_handle: _control_handle,
    }) = next_request(&mut stream).await?
    {
        if !quiet {
            trace!("Received service request for service");
        }
        let chan =
            fidl::AsyncChannel::from_channel(chan).context("failed to make async channel")?;
        spawn_echo_server(chan, quiet);
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// main

async fn async_main() -> Result<(), Error> {
    let args = app().get_matches();

    let svc = hoist::connect()?;

    match args.subcommand() {
        ("server", Some(_)) => exec_server(svc, args.is_present("quiet")).await,
        ("client", Some(cmd)) => {
            let r = exec_client(svc, cmd.value_of("text")).await;
            trace!("finished client");
            r
        }
        (_, _) => unimplemented!(),
    }
}

fn main() {
    hoist::run(async move {
        if let Err(e) = async_main().await {
            trace!("Error: {}", e)
        }
    })
}
