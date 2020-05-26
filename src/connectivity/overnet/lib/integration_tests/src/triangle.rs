// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Triangular overnet tests - tests that involve passing a channel amongst at least three nodes

#![cfg(test)]

use {
    crate::Overnet,
    anyhow::{Context as _, Error},
    fidl::endpoints::{ClientEnd, RequestStream, ServerEnd, ServiceMarker},
    fidl_fidl_examples_echo as echo,
    fidl_fuchsia_overnet::{
        Peer, ServiceConsumerProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
        ServicePublisherProxyInterface,
    },
    fidl_fuchsia_overnet_triangletests as triangle,
    futures::prelude::*,
    overnet_core::{spawn, NodeId},
    std::sync::Arc,
};

////////////////////////////////////////////////////////////////////////////////
// Test scenarios

#[test]
fn simple_loop() -> Result<(), Error> {
    crate::run_async_test(async move {
        // Three nodes, fully connected
        // A creates a channel, passes either end to B, C to do an echo request
        let a = Overnet::new()?;
        let b = Overnet::new()?;
        let c = Overnet::new()?;
        crate::connect(&a, &b)?;
        crate::connect(&b, &c)?;
        crate::connect(&a, &c)?;
        run_triangle_echo_test(
            b.node_id(),
            c.node_id(),
            a,
            &[],
            &[b, c],
            Some("HELLO INTEGRATION TEST WORLD"),
        )
        .await
    })
}

#[test]
fn simple_flat() -> Result<(), Error> {
    crate::run_async_test(async move {
        // Three nodes, connected linearly: C - A - B
        // A creates a channel, passes either end to B, C to do an echo request
        let a = Overnet::new()?;
        let b = Overnet::new()?;
        let c = Overnet::new()?;
        crate::connect(&a, &b)?;
        crate::connect(&a, &c)?;
        run_triangle_echo_test(
            b.node_id(),
            c.node_id(),
            a,
            &[],
            &[b, c],
            Some("HELLO INTEGRATION TEST WORLD"),
        )
        .await
    })
}

#[test]
fn full_transfer() -> Result<(), Error> {
    crate::run_async_test(async move {
        // Two nodes connected
        // A creates a channel, passes both ends to B to do an echo request
        let a = Overnet::new()?;
        let b = Overnet::new()?;
        crate::connect(&a, &b)?;
        run_triangle_echo_test(
            b.node_id(),
            b.node_id(),
            a,
            &[],
            &[b],
            Some("HELLO INTEGRATION TEST WORLD"),
        )
        .await
    })
}

#[test]
fn forwarded_twice_to_separate_nodes() -> Result<(), Error> {
    crate::run_async_test(async move {
        // Five nodes connected in a loop: A - B - C - D - E - A
        // A creates a channel, passes either end to B & C
        // B & C forward to D & E (respectively) and then do an echo request
        let a = Overnet::new()?;
        let b = Overnet::new()?;
        let c = Overnet::new()?;
        let d = Overnet::new()?;
        let e = Overnet::new()?;
        log::info!(
            "NODEIDS:-> A:{:?} B:{:?} C:{:?} D:{:?} E:{:?}",
            a.node_id(),
            b.node_id(),
            c.node_id(),
            d.node_id(),
            e.node_id()
        );
        crate::connect(&a, &b)?;
        crate::connect(&b, &c)?;
        crate::connect(&c, &d)?;
        crate::connect(&d, &e)?;
        crate::connect(&e, &a)?;
        run_triangle_echo_test(
            b.node_id(),
            c.node_id(),
            a,
            &[(b, d.node_id()), (c, e.node_id())],
            &[d, e],
            Some("HELLO INTEGRATION TEST WORLD"),
        )
        .await
    })
}

#[test]
fn forwarded_twice_full_transfer() -> Result<(), Error> {
    crate::run_async_test(async move {
        // Four nodes connected in a line: A - B - C - D
        // A creates a channel, passes either end to B & C
        // B & C forward to D which then does an echo request
        let a = Overnet::new()?;
        let b = Overnet::new()?;
        let c = Overnet::new()?;
        let d = Overnet::new()?;
        crate::connect(&a, &b)?;
        crate::connect(&b, &c)?;
        crate::connect(&c, &d)?;
        run_triangle_echo_test(
            b.node_id(),
            c.node_id(),
            a,
            &[(b, d.node_id()), (c, d.node_id())],
            &[d],
            Some("HELLO INTEGRATION TEST WORLD"),
        )
        .await
    })
}

////////////////////////////////////////////////////////////////////////////////
// Utilities

fn has_peer_conscript(peers: &[Peer], peer_id: NodeId) -> bool {
    let is_peer_ready = |peer: &Peer| -> bool {
        peer.id == peer_id.into()
            && peer.description.services.is_some()
            && peer
                .description
                .services
                .as_ref()
                .unwrap()
                .iter()
                .find(|name| *name == triangle::ConscriptMarker::NAME)
                .is_some()
    };
    peers.iter().find(|p| is_peer_ready(p)).is_some()
}

fn connect_peer(
    svc: &impl ServiceConsumerProxyInterface,
    node_id: NodeId,
) -> Result<triangle::ConscriptProxy, Error> {
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    svc.connect_to_service(&mut node_id.into(), triangle::ConscriptMarker::NAME, s).unwrap();
    let proxy = fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
    Ok(triangle::ConscriptProxy::new(proxy))
}

////////////////////////////////////////////////////////////////////////////////
// Client implementation

async fn exec_captain(
    client: NodeId,
    server: NodeId,
    overnet: Arc<Overnet>,
    text: Option<&str>,
) -> Result<(), Error> {
    let svc = overnet.connect_as_service_consumer()?;
    loop {
        let peers = svc.list_peers().await?;
        log::info!("Got peers: {:?}", peers);
        if has_peer_conscript(&peers, client) && has_peer_conscript(&peers, server) {
            let client = connect_peer(&svc, client)?;
            let server = connect_peer(&svc, server)?;
            let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
            log::info!("server/proxy hdls: {:?} {:?}", s, p);
            log::info!("ENGAGE CONSCRIPTS");
            server.serve(ServerEnd::new(s))?;
            assert_eq!(client.issue(ClientEnd::new(p), text).await?, text.map(|s| s.to_string()));
            return Ok(());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Conscript implementation

async fn exec_server(server: ServerEnd<echo::EchoMarker>) -> Result<(), Error> {
    let mut stream = server.into_stream()?;
    while let Some(echo::EchoRequest::EchoString { value, responder }) =
        stream.try_next().await.context("error running echo server")?
    {
        log::info!("Received echo request for string {:?}", value);
        responder.send(value.as_ref().map(|s| &**s)).context("error sending response")?;
        log::info!("echo response sent successfully");
    }
    Ok(())
}

async fn exec_client(
    client: ClientEnd<echo::EchoMarker>,
    text: Option<String>,
    responder: triangle::ConscriptIssueResponder,
) -> Result<(), Error> {
    log::info!("CLIENT SEND REQUEST: {:?}", text);
    let response = client.into_proxy()?.echo_string(text.as_deref()).await.unwrap();
    log::info!("CLIENT GETS RESPONSE: {:?}", response);
    responder.send(response.as_deref())?;
    Ok(())
}

async fn exec_conscript(
    overnet: Arc<Overnet>,
    action: impl 'static + Clone + Fn(triangle::ConscriptRequest) -> Result<(), Error>,
) -> Result<(), Error> {
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
    let mut stream = ServiceProviderRequestStream::from_channel(chan);
    let node_id = overnet.node_id();
    overnet
        .connect_as_service_publisher()?
        .publish_service(triangle::ConscriptMarker::NAME, ClientEnd::new(p))?;
    while let Some(ServiceProviderRequest::ConnectToService { chan, info: _, control_handle: _ }) =
        stream.try_next().await?
    {
        log::info!("{:?} Received service request for service", node_id);
        let chan =
            fidl::AsyncChannel::from_channel(chan).context("failed to make async channel")?;
        let action = action.clone();
        spawn(
            async move {
                let mut stream = triangle::ConscriptRequestStream::from_channel(chan);
                log::info!("{:?} Started service handler", node_id);
                while let Some(request) = stream.try_next().await? {
                    log::info!("{:?} Received request {:?}", node_id, request);
                    action(request)?;
                }
                log::info!("{:?} Finished service handler", node_id);
                Ok(())
            }
            .unwrap_or_else(move |e: Error| log::info!("{:?} failed {:?}", node_id, e)),
        );
    }
    Ok(())
}

fn conscript_leaf_action(request: triangle::ConscriptRequest) -> Result<(), Error> {
    log::info!("Handling it");
    match request {
        triangle::ConscriptRequest::Serve { iface, control_handle: _ } => {
            spawn(exec_server(iface).unwrap_or_else(|e| log::warn!("{:?}", e)))
        }
        triangle::ConscriptRequest::Issue { iface, request, responder } => {
            spawn(exec_client(iface, request, responder).unwrap_or_else(|e| log::warn!("{:?}", e)))
        }
    }
    Ok(())
}

fn conscript_forward_action(
    node_id: NodeId,
    request: triangle::ConscriptRequest,
    target: triangle::ConscriptProxy,
) -> Result<(), Error> {
    log::info!("Forwarding request to {:?}", node_id);
    match request {
        triangle::ConscriptRequest::Serve { iface, control_handle: _ } => {
            target.serve(iface)?;
        }
        triangle::ConscriptRequest::Issue { iface, request, responder } => spawn(
            async move {
                responder.send(target.issue(iface, request.as_deref()).await?.as_deref())?;
                Ok(())
            }
            .unwrap_or_else(|e: Error| log::warn!("{:?}", e)),
        ),
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// Test driver

async fn run_triangle_echo_test(
    client: NodeId,
    server: NodeId,
    captain: Arc<Overnet>,
    forwarders: &[(Arc<Overnet>, NodeId)],
    conscripts: &[Arc<Overnet>],
    text: Option<&str>,
) -> Result<(), Error> {
    for (forwarder, target_node_id) in forwarders {
        let forwarder = forwarder.clone();
        let target_node_id = *target_node_id;
        spawn(
            async move {
                let svc = forwarder.clone().connect_as_service_consumer()?;
                loop {
                    let peers = svc.list_peers().await?;
                    log::info!(
                        "Waiting for forwarding target {:?}; got peers {:?}",
                        target_node_id,
                        peers
                    );
                    if has_peer_conscript(&peers, target_node_id) {
                        break;
                    }
                }
                let target = connect_peer(&svc, target_node_id)?;
                exec_conscript(forwarder, move |request| {
                    conscript_forward_action(target_node_id, request, target.clone())
                })
                .await
            }
            .unwrap_or_else(|e: Error| log::warn!("{:?}", e)),
        )
    }
    for conscript in conscripts {
        let conscript = conscript.clone();
        spawn(async move { exec_conscript(conscript, conscript_leaf_action).await.unwrap() });
    }
    exec_captain(client, server, captain, text).await
}
