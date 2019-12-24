// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    argh::FromArgs,
    fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker},
    fidl_fidl_examples_echo as echo,
    fidl_fuchsia_overnet::{
        ServiceConsumerProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
    },
    futures::prelude::*,
};

#[derive(FromArgs)]
/// Echo example for Overnet.
struct OvernetEcho {
    #[argh(switch, short = 'q')]
    /// whether or not to silence logs
    quiet: bool,

    #[argh(subcommand)]
    subcommand: Subcommand,
}

#[derive(FromArgs)]
#[argh(subcommand, name = "server", description = "run as server")]
struct ServerCommand {}

#[derive(FromArgs)]
#[argh(subcommand, name = "client", description = "run as client")]
struct ClientCommand {
    #[argh(positional)]
    /// text string to echo back and forth
    text: Option<String>,
}

#[derive(FromArgs)]
#[argh(subcommand)]
enum Subcommand {
    Server(ServerCommand),
    Client(ClientCommand),
}

////////////////////////////////////////////////////////////////////////////////
// Client implementation

async fn exec_client(text: Option<String>) -> Result<(), Error> {
    let svc = hoist::connect_as_service_consumer()?;
    loop {
        let peers = svc.list_peers().await?;
        log::trace!("Got peers: {:?}", peers);
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

            log::trace!("Trying peer: {:?}", peer.id);

            let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
            if let Err(e) = svc.connect_to_service(&mut peer.id, echo::EchoMarker::NAME, s) {
                log::trace!("{:?}", e);
                continue;
            }
            let proxy =
                fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
            let cli = echo::EchoProxy::new(proxy);
            log::trace!("Sending {:?} to {:?}", text, peer.id);
            match cli.echo_string(text.as_ref().map(|s| s.as_str())).await {
                Ok(r) => {
                    log::trace!("SUCCESS: received {:?}", r);
                    return Ok(());
                }
                Err(e) => {
                    log::trace!("ERROR: {:?}", e);
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
                    log::trace!("Received echo request for string {:?}", value);
                }
                responder.send(value.as_ref().map(|s| &**s)).context("error sending response")?;
                if !quiet {
                    log::trace!("echo response sent successfully");
                }
            }
            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| log::trace!("ERROR: {:?}", e)),
    );
}

async fn next_request(
    stream: &mut ServiceProviderRequestStream,
) -> Result<Option<ServiceProviderRequest>, Error> {
    log::trace!("Awaiting request");
    Ok(stream.try_next().await.context("error running service provider server")?)
}

async fn exec_server(quiet: bool) -> Result<(), Error> {
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
    let mut stream = ServiceProviderRequestStream::from_channel(chan);
    hoist::publish_service(echo::EchoMarker::NAME, ClientEnd::new(p))?;
    while let Some(ServiceProviderRequest::ConnectToService {
        chan,
        info: _,
        control_handle: _control_handle,
    }) = next_request(&mut stream).await?
    {
        if !quiet {
            log::trace!("Received service request for service");
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
    let app: OvernetEcho = argh::from_env();

    match app.subcommand {
        Subcommand::Server(_) => exec_server(app.quiet).await,
        Subcommand::Client(c) => {
            let r = exec_client(c.text).await;
            log::trace!("finished client");
            r
        }
    }
}

fn main() {
    hoist::run(async move {
        if let Err(e) = async_main().await {
            log::trace!("Error: {}", e)
        }
    })
}
