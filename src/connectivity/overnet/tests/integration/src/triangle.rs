// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Triangular overnet tests - tests that involve passing a channel amongst at least three nodes

#![cfg(test)]

use {
    super::Overnet,
    anyhow::{Context as _, Error},
    fidl::{
        endpoints::{ClientEnd, ServerEnd},
        prelude::*,
    },
    fidl_fuchsia_overnet::{
        Peer, ServiceConsumerProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
        ServicePublisherProxyInterface,
    },
    fidl_fuchsia_overnet_triangletests as triangle, fidl_test_placeholders as echo,
    fuchsia_async::Task,
    futures::prelude::*,
    overnet_core::{NodeId, NodeIdGenerator},
    std::sync::Arc,
};

////////////////////////////////////////////////////////////////////////////////
// Test scenarios

#[fuchsia::test]
async fn simple_loop(run: usize) -> Result<(), Error> {
    let mut node_id_gen = NodeIdGenerator::new("simple_loop", run);
    // Three nodes, fully connected
    // A creates a channel, passes either end to B, C to do an echo request
    let a = Overnet::new(&mut node_id_gen)?;
    let b = Overnet::new(&mut node_id_gen)?;
    let c = Overnet::new(&mut node_id_gen)?;
    super::connect(&a, &b)?;
    super::connect(&b, &c)?;
    super::connect(&a, &c)?;
    run_triangle_echo_test(
        b.node_id(),
        c.node_id(),
        a,
        vec![],
        vec![b, c],
        Some("HELLO INTEGRATION TEST WORLD"),
    )
    .await
}

#[fuchsia::test]
async fn simple_flat(run: usize) -> Result<(), Error> {
    let mut node_id_gen = NodeIdGenerator::new("simple_flat", run);
    // Three nodes, connected linearly: C - A - B
    // A creates a channel, passes either end to B, C to do an echo request
    let a = Overnet::new(&mut node_id_gen)?;
    let b = Overnet::new(&mut node_id_gen)?;
    let c = Overnet::new(&mut node_id_gen)?;
    super::connect(&a, &b)?;
    super::connect(&a, &c)?;
    run_triangle_echo_test(
        b.node_id(),
        c.node_id(),
        a,
        vec![],
        vec![b, c],
        Some("HELLO INTEGRATION TEST WORLD"),
    )
    .await
}

#[fuchsia::test]
async fn full_transfer(run: usize) -> Result<(), Error> {
    let mut node_id_gen = NodeIdGenerator::new("full_transfer", run);
    // Two nodes connected
    // A creates a channel, passes both ends to B to do an echo request
    let a = Overnet::new(&mut node_id_gen)?;
    let b = Overnet::new(&mut node_id_gen)?;
    super::connect(&a, &b)?;
    run_triangle_echo_test(
        b.node_id(),
        b.node_id(),
        a,
        vec![],
        vec![b],
        Some("HELLO INTEGRATION TEST WORLD"),
    )
    .await
}

#[fuchsia::test]
#[ignore = "TODO(https://fxbug.dev/110501): Re-enable after CSO lands"]
async fn forwarded_twice_to_separate_nodes(run: usize) -> Result<(), Error> {
    let mut node_id_gen = NodeIdGenerator::new("forwarded_twice_to_separate_nodes", run);
    // Five nodes connected in a line: A - B - C - D - E
    // A creates a channel, passes either end to B & C
    // B & C forward to D & E (respectively) and then do an echo request
    let a = Overnet::new(&mut node_id_gen)?;
    let b = Overnet::new(&mut node_id_gen)?;
    let c = Overnet::new(&mut node_id_gen)?;
    let d = Overnet::new(&mut node_id_gen)?;
    let e = Overnet::new(&mut node_id_gen)?;
    tracing::info!(
        a = a.node_id().0,
        b = b.node_id().0,
        c = c.node_id().0,
        d = d.node_id().0,
        e = e.node_id().0,
        "NODEIDS"
    );
    super::connect(&a, &b)?;
    super::connect(&b, &c)?;
    super::connect(&c, &d)?;
    super::connect(&d, &e)?;
    run_triangle_echo_test(
        b.node_id(),
        c.node_id(),
        a,
        vec![(b, d.node_id()), (c, e.node_id())],
        vec![d, e],
        Some("HELLO INTEGRATION TEST WORLD"),
    )
    .await
}

#[fuchsia::test]
#[ignore = "TODO(https://fxbug.dev/110501): Re-enable after CSO lands"]
async fn forwarded_twice_full_transfer(run: usize) -> Result<(), Error> {
    let mut node_id_gen = NodeIdGenerator::new("forwarded_twice_full_transfer", run);
    // Four nodes connected in a line: A - B - C - D
    // A creates a channel, passes either end to B & C
    // B & C forward to D which then does an echo request
    let a = Overnet::new(&mut node_id_gen)?;
    let b = Overnet::new(&mut node_id_gen)?;
    let c = Overnet::new(&mut node_id_gen)?;
    let d = Overnet::new(&mut node_id_gen)?;
    super::connect(&a, &b)?;
    super::connect(&b, &c)?;
    super::connect(&c, &d)?;
    run_triangle_echo_test(
        b.node_id(),
        c.node_id(),
        a,
        vec![(b, d.node_id()), (c, d.node_id())],
        vec![d],
        Some("HELLO INTEGRATION TEST WORLD"),
    )
    .await
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
                .any(|name| *name == triangle::ConscriptMarker::PROTOCOL_NAME)
    };
    peers.iter().any(|p| is_peer_ready(p))
}

fn connect_peer(
    svc: &impl ServiceConsumerProxyInterface,
    node_id: NodeId,
) -> Result<triangle::ConscriptProxy, Error> {
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    svc.connect_to_service(&mut node_id.into(), triangle::ConscriptMarker::PROTOCOL_NAME, s)
        .unwrap();
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
        tracing::info!(node_id = overnet.node_id().0, "Got peers: {:?}", peers);
        if has_peer_conscript(&peers, client) && has_peer_conscript(&peers, server) {
            let client = connect_peer(&svc, client)?;
            let server = connect_peer(&svc, server)?;
            let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
            tracing::info!(node_id = overnet.node_id().0, "server/proxy hdls: {:?} {:?}", s, p);
            tracing::info!(node_id = overnet.node_id().0, "ENGAGE CONSCRIPTS");
            server.serve(ServerEnd::new(s))?;
            let response = client
                .issue(ClientEnd::new(p), text)
                .await
                .context(format!("awaiting issue response for captain {:?}", overnet.node_id()))?;
            tracing::info!(node_id = overnet.node_id().0, "Captain got response: {:?}", response);
            assert_eq!(response, text.map(|s| s.to_string()));
            return Ok(());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Conscript implementation

async fn exec_server(node_id: NodeId, server: ServerEnd<echo::EchoMarker>) -> Result<(), Error> {
    let mut stream = server.into_stream()?;
    tracing::info!(node_id = node_id.0, "server begins");
    while let Some(echo::EchoRequest::EchoString { value, responder }) =
        stream.try_next().await.context("error running echo server")?
    {
        tracing::info!(node_id = node_id.0, "Received echo request for string {:?}", value);
        responder.send(value.as_ref().map(|s| &**s)).context("error sending response")?;
        tracing::info!(node_id = node_id.0, "echo response sent successfully");
    }
    tracing::info!(node_id = node_id.0, "server done");
    Ok(())
}

async fn exec_client(
    node_id: NodeId,
    client: ClientEnd<echo::EchoMarker>,
    text: Option<String>,
    responder: triangle::ConscriptIssueResponder,
) -> Result<(), Error> {
    tracing::info!(node_id = node_id.0, "CLIENT SEND REQUEST: {:?}", text);
    let response = client.into_proxy()?.echo_string(text.as_deref()).await.unwrap();
    tracing::info!(node_id = node_id.0, "CLIENT GETS RESPONSE: {:?}", response);
    responder.send(response.as_deref())?;
    Ok(())
}

async fn exec_conscript<
    F: 'static + Send + Clone + Fn(triangle::ConscriptRequest) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
>(
    overnet: Arc<Overnet>,
    action: F,
) -> Result<(), Error> {
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
    let node_id = overnet.node_id();
    overnet
        .connect_as_service_publisher()?
        .publish_service(triangle::ConscriptMarker::PROTOCOL_NAME, ClientEnd::new(p))?;
    ServiceProviderRequestStream::from_channel(chan)
        .map_err(Into::into)
        .try_for_each_concurrent(None, |req| {
            let action = action.clone();
            async move {
                let _ = &req;
                let ServiceProviderRequest::ConnectToService { chan, info: _, control_handle: _ } =
                    req;
                tracing::info!(node_id = node_id.0, "Received service request for service");
                let chan = fidl::AsyncChannel::from_channel(chan)
                    .context("failed to make async channel")?;
                tracing::info!(node_id = node_id.0, "Started service handler");
                triangle::ConscriptRequestStream::from_channel(chan)
                    .map_err(Into::into)
                    .try_for_each_concurrent(None, |request| {
                        let action = action.clone();
                        async move {
                            tracing::info!(node_id = node_id.0, "Received request {:?}", request);
                            action(request).await
                        }
                    })
                    .await?;
                tracing::info!(node_id = node_id.0, "Finished service handler");
                Ok(())
            }
        })
        .await
}

async fn conscript_leaf_action(
    own_id: NodeId,
    request: triangle::ConscriptRequest,
) -> Result<(), Error> {
    tracing::info!(node_id = own_id.0, "Handling it");
    match request {
        triangle::ConscriptRequest::Serve { iface, control_handle: _ } => {
            exec_server(own_id, iface)
                .await
                .context(format!("running conscript server {:?}", own_id))
        }
        triangle::ConscriptRequest::Issue { iface, request, responder } => {
            exec_client(own_id, iface, request, responder)
                .await
                .context(format!("running conscript client {:?}", own_id))
        }
    }
}

async fn conscript_forward_action(
    own_id: NodeId,
    node_id: NodeId,
    request: triangle::ConscriptRequest,
    target: triangle::ConscriptProxy,
) -> Result<(), Error> {
    tracing::info!(node_id = own_id.0, "Forwarding request to {:?}", node_id);
    match request {
        triangle::ConscriptRequest::Serve { iface, control_handle: _ } => {
            target.serve(iface)?;
        }
        triangle::ConscriptRequest::Issue { iface, request, responder } => {
            let response = target.issue(iface, request.as_deref()).await?;
            tracing::info!("Forwarder got response: {:?}", response);
            responder.send(response.as_deref())?;
        }
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// Test driver

async fn run_triangle_echo_test(
    client: NodeId,
    server: NodeId,
    captain: Arc<Overnet>,
    forwarders: Vec<(Arc<Overnet>, NodeId)>,
    conscripts: Vec<Arc<Overnet>>,
    text: Option<&str>,
) -> Result<(), Error> {
    let captain_node_id = captain.node_id();
    let mut background_tasks = Vec::new();
    for (forwarder, target_node_id) in forwarders.into_iter() {
        background_tasks.push(Task::spawn(
            async move {
                let svc = forwarder.clone().connect_as_service_consumer()?;
                loop {
                    let peers = svc.list_peers().await?;
                    tracing::info!(
                        "Waiting for forwarding target {:?}; got peers {:?}",
                        target_node_id,
                        peers
                    );
                    if has_peer_conscript(&peers, target_node_id) {
                        break;
                    }
                }
                let target = connect_peer(&svc, target_node_id)?;
                let own_id = forwarder.node_id();
                exec_conscript(forwarder, move |request| {
                    conscript_forward_action(own_id, target_node_id, request, target.clone())
                })
                .await
            }
            .unwrap_or_else(|e: Error| tracing::warn!("{:?}", e)),
        ));
    }
    for conscript in conscripts.into_iter() {
        let conscript = conscript.clone();
        let own_id = conscript.node_id();
        background_tasks.push(Task::spawn(async move {
            exec_conscript(conscript, move |request| conscript_leaf_action(own_id, request))
                .await
                .unwrap()
        }));
    }
    exec_captain(client, server, captain, text).await?;
    for (i, task) in background_tasks.into_iter().enumerate() {
        tracing::info!(node_id = captain_node_id.0, "drop background task {}", i);
        drop(task);
    }
    tracing::info!(node_id = captain_node_id.0, "returning from test driver");
    Ok(())
}
