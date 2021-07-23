// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    argh::FromArgs,
    fidl::endpoints::{ClientEnd, ProtocolMarker, RequestStream},
    fidl::{Peered, Signals},
    fidl_fuchsia_overnet::{ServiceProviderRequest, ServiceProviderRequestStream},
    fidl_fuchsia_overnet_eventpairexample as event_pair,
    fuchsia_async::{OnSignals, Task},
    futures::prelude::*,
    hoist::{hoist, OvernetInstance},
};

#[derive(FromArgs)]
/// Echo example for Overnet.
struct TestArgs {
    #[argh(subcommand)]
    subcommand: Subcommand,
}

#[derive(FromArgs, Clone)]
#[argh(subcommand, name = "server")]
/// Run as a server
struct ServerCommand {}

#[derive(FromArgs)]
#[argh(subcommand, name = "client")]
/// Run as a server
struct ClientCommand {}

#[derive(FromArgs)]
#[argh(subcommand)]
enum Subcommand {
    Server(ServerCommand),
    Client(ClientCommand),
}

////////////////////////////////////////////////////////////////////////////////
// Client implementation

async fn exec_client() -> Result<(), Error> {
    let svc = hoist().connect_as_service_consumer()?;
    loop {
        let peers = svc.list_peers().await?;
        println!("Got peers: {:?}", peers);
        for mut peer in peers {
            if peer.description.services.is_none() {
                continue;
            }
            if peer
                .description
                .services
                .unwrap()
                .iter()
                .find(|name| *name == event_pair::ExampleMarker::NAME)
                .is_none()
            {
                continue;
            }
            let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
            if let Err(e) = svc.connect_to_service(&mut peer.id, event_pair::ExampleMarker::NAME, s)
            {
                println!("{:?}", e);
                continue;
            }
            let proxy =
                fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
            let cli = event_pair::ExampleProxy::new(proxy);

            let (cev, sev) = fidl::EventPair::create().context("failed to create event pair")?;
            let r = Task::spawn(cli.pass(sev));
            println!("signal peer on 0");
            cev.signal_peer(Signals::empty(), Signals::USER_0)?;
            println!("await on 1");
            assert_eq!(
                OnSignals::new(&cev, Signals::USER_1).await? & Signals::USER_1,
                Signals::USER_1
            );
            println!("signal peer on 2");
            cev.signal_peer(Signals::empty(), Signals::USER_2)?;
            println!("await completion");
            r.await?;
            return Ok(());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Server implementation

async fn example_server(chan: fidl::AsyncChannel) -> Result<(), Error> {
    let mut stream = event_pair::ExampleRequestStream::from_channel(chan);
    while let Some(request) = stream.try_next().await.context("error running echo server")? {
        match request {
            event_pair::ExampleRequest::Pass { event_pair, responder } => {
                println!("Received event_pair request; await on 0");
                assert_eq!(
                    OnSignals::new(&event_pair, Signals::USER_0).await? & Signals::USER_0,
                    Signals::USER_0
                );
                println!("signal peer on 1");
                event_pair.signal_peer(Signals::empty(), Signals::USER_1)?;
                println!("await on 2");
                assert_eq!(
                    OnSignals::new(&event_pair, Signals::USER_2).await? & Signals::USER_2,
                    Signals::USER_2
                );
                println!("return");
                responder.send()?;
            }
        }
    }
    Ok(())
}

async fn exec_server() -> Result<(), Error> {
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
    hoist().publish_service(event_pair::ExampleMarker::NAME, ClientEnd::new(p))?;
    ServiceProviderRequestStream::from_channel(chan)
        .map_err(Into::into)
        .try_for_each_concurrent(
            None,
            |ServiceProviderRequest::ConnectToService {
                 chan,
                 info: _,
                 control_handle: _control_handle,
             }| {
                async move {
                    println!("Received service request for service");
                    let chan = fidl::AsyncChannel::from_channel(chan)
                        .context("failed to make async channel")?;
                    example_server(chan).await
                }
            },
        )
        .await
}

////////////////////////////////////////////////////////////////////////////////
// main

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    match argh::from_env::<TestArgs>().subcommand {
        Subcommand::Server(_server_args) => exec_server().await,
        Subcommand::Client(_client_args) => exec_client().await,
    }
}
