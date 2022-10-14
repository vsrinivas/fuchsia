// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::{endpoints::ClientEnd, prelude::*},
    fidl_fuchsia_developer_remotecontrol as rcs,
    fidl_fuchsia_overnet::{ServiceProviderRequest, ServiceProviderRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::join,
    futures::prelude::*,
    hoist::{hoist, OvernetInstance},
    remote_control::RemoteControlService,
    std::rc::Rc,
    tracing::{error, info},
};

#[cfg(feature = "circuit")]
use std::sync::Arc;

mod args;

async fn exec_server() -> Result<(), Error> {
    diagnostics_log::init!(&["remote-control"]);

    #[cfg(feature = "circuit")]
    let router = overnet_core::Router::new(
        overnet_core::RouterOptions::new(),
        Box::new(overnet_core::SimpleSecurityContext {
            node_cert: "/pkg/data/cert.crt",
            node_private_key: "/pkg/data/cert.key",
            root_cert: "/pkg/data/rootca.crt",
        }),
    )?;

    #[cfg(feature = "circuit")]
    let connector = {
        let router = Arc::clone(&router);
        move |socket| {
            let router = Arc::clone(&router);
            fasync::Task::spawn(async move {
                match fidl::AsyncSocket::from_socket(socket) {
                    Ok(socket) => {
                        let (mut rx, mut tx) = socket.split();
                        if let Err(e) =
                            circuit::multi_stream::multi_stream_node_connection_to_async(
                                router.circuit_node(),
                                &mut rx,
                                &mut tx,
                                true,
                                circuit::Quality::NETWORK,
                            )
                            .await
                        {
                            error!("Error handling Overnet link: {:?}", e);
                        }
                    }
                    Err(e) => error!("Could not handle incoming link socket: {:?}", e),
                }
            })
            .detach();
        }
    };

    #[cfg(not(feature = "circuit"))]
    let connector = |_| error!("Circuit connections not supported");

    let service = Rc::new(RemoteControlService::new(connector).await);

    #[cfg(feature = "circuit")]
    let onet_circuit_fut = {
        let (s, p) = fidl::Channel::create().context("creating ServiceProvider zx channel")?;
        let chan = fidl::AsyncChannel::from_channel(s)
            .context("creating ServiceProvider async channel")?;
        let stream = ServiceProviderRequestStream::from_channel(chan);
        router
            .register_service(rcs::RemoteControlMarker::PROTOCOL_NAME.to_owned(), ClientEnd::new(p))
            .await?;
        let sc = service.clone();
        async move {
            let fut = stream.for_each_concurrent(None, move |svc| {
                let ServiceProviderRequest::ConnectToService {
                    chan,
                    info: _,
                    control_handle: _control_handle,
                } = svc.unwrap();
                let chan = fidl::AsyncChannel::from_channel(chan)
                    .context("failed to make async channel")
                    .unwrap();

                sc.clone().serve_stream(rcs::RemoteControlRequestStream::from_channel(chan))
            });
            info!("published remote control service to overnet");
            let res = fut.await;
            info!("connection to overnet lost: {:?}", res);
        }
    };

    let sc = service.clone();
    let onet_fut = async move {
        loop {
            let sc = sc.clone();
            let stream = (|| -> Result<_, Error> {
                let (s, p) =
                    fidl::Channel::create().context("creating ServiceProvider zx channel")?;
                let chan = fidl::AsyncChannel::from_channel(s)
                    .context("creating ServiceProvider async channel")?;
                let stream = ServiceProviderRequestStream::from_channel(chan);
                hoist()
                    .publish_service(rcs::RemoteControlMarker::PROTOCOL_NAME, ClientEnd::new(p))?;
                Ok(stream)
            })();

            let stream = match stream {
                Ok(stream) => stream,
                Err(err) => {
                    error!("Could not connect to overnet: {:?}", err);
                    break;
                }
            };

            let fut = stream.for_each_concurrent(None, move |svc| {
                let ServiceProviderRequest::ConnectToService {
                    chan,
                    info: _,
                    control_handle: _control_handle,
                } = svc.unwrap();
                let chan = fidl::AsyncChannel::from_channel(chan)
                    .context("failed to make async channel")
                    .unwrap();

                sc.clone().serve_stream(rcs::RemoteControlRequestStream::from_channel(chan))
            });
            info!("published remote control service to overnet");
            let res = fut.await;
            info!("connection to overnet lost: {:?}", res);
        }
    };

    let sc1 = service.clone();
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |req| {
        fasync::Task::local(sc1.clone().serve_stream(req)).detach();
    });

    fs.take_and_serve_directory_handle()?;
    let fidl_fut = fs.collect::<()>();

    #[cfg(feature = "circuit")]
    join!(fidl_fut, onet_fut, onet_circuit_fut);
    #[cfg(not(feature = "circuit"))]
    join!(fidl_fut, onet_fut);
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args::RemoteControl { cmd } = argh::from_env();

    let res = match cmd {
        args::Command::DiagnosticsBridge(_) => diagnostics_bridge::exec_server().await,
        args::Command::RemoteControl(_) => exec_server().await,
    };

    if let Err(err) = res {
        error!(%err, "Error running command");
        std::process::exit(1);
    }
    Ok(())
}
