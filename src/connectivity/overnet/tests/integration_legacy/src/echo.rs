// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Echo integration test for Fuchsia (much like the echo example - indeed the initial code came
//! from there, but self contained and more adaptable to different scenarios)

#![cfg(test)]

use {
    super::Overnet,
    anyhow::{Context as _, Error},
    fidl::{endpoints::ClientEnd, prelude::*},
    fidl_fuchsia_overnet::{
        ServiceConsumerProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
        ServicePublisherProxyInterface,
    },
    fidl_test_placeholders as echo,
    fuchsia_async::Task,
    futures::prelude::*,
    overnet_core::NodeIdGenerator,
    std::sync::Arc,
};

////////////////////////////////////////////////////////////////////////////////
// Test scenarios

#[fuchsia::test]
async fn simple(run: usize) -> Result<(), Error> {
    let mut node_id_gen = NodeIdGenerator::new("simple", run);
    let client = Overnet::new(&mut node_id_gen)?;
    let server = Overnet::new(&mut node_id_gen)?;
    super::connect(&client, &server)?;
    run_echo_test(client, server, Some("HELLO INTEGRATION TEST WORLD")).await
}

#[fuchsia::test]
async fn kilobyte(run: usize) -> Result<(), Error> {
    let mut node_id_gen = NodeIdGenerator::new("kilobyte", run);
    let client = Overnet::new(&mut node_id_gen)?;
    let server = Overnet::new(&mut node_id_gen)?;
    super::connect(&client, &server)?;
    run_echo_test(client, server, Some(&std::iter::repeat('a').take(1024).collect::<String>()))
        .await
}

#[fuchsia::test]
async fn quite_large(run: usize) -> Result<(), Error> {
    let mut node_id_gen = NodeIdGenerator::new("quite_large", run);
    let client = Overnet::new(&mut node_id_gen)?;
    let server = Overnet::new(&mut node_id_gen)?;
    super::connect(&client, &server)?;
    run_echo_test(client, server, Some(&std::iter::repeat('a').take(60000).collect::<String>()))
        .await
}

#[fuchsia::test]
async fn quic(run: usize) -> Result<(), Error> {
    let mut node_id_gen = NodeIdGenerator::new("quic", run);
    let client = Overnet::new(&mut node_id_gen)?;
    let server = Overnet::new(&mut node_id_gen)?;
    super::connect_with_quic(&client, &server)?;
    run_echo_test(client, server, Some("HELLO INTEGRATION TEST WORLD")).await
}

////////////////////////////////////////////////////////////////////////////////
// Client implementation

async fn exec_client(overnet: Arc<Overnet>, text: Option<&str>) -> Result<(), Error> {
    let svc = overnet.connect_as_service_consumer()?;
    loop {
        let peers = svc.list_peers().await?;
        tracing::info!(node_id = overnet.node_id().0, "Got peers: {:?}", peers);
        for mut peer in peers {
            if peer.description.services.is_none() {
                continue;
            }
            if peer
                .description
                .services
                .unwrap()
                .iter()
                .find(|name| *name == echo::EchoMarker::PROTOCOL_NAME)
                .is_none()
            {
                continue;
            }
            let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
            svc.connect_to_service(&mut peer.id, echo::EchoMarker::PROTOCOL_NAME, s).unwrap();
            let proxy =
                fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
            let cli = echo::EchoProxy::new(proxy);
            tracing::info!(node_id = overnet.node_id().0, "Sending {:?} to {:?}", text, peer.id);
            assert_eq!(cli.echo_string(text).await.unwrap(), text.map(|s| s.to_string()));
            return Ok(());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Server implementation

async fn exec_server(overnet: Arc<Overnet>) -> Result<(), Error> {
    let node_id = overnet.node_id();
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
    overnet
        .connect_as_service_publisher()?
        .publish_service(echo::EchoMarker::PROTOCOL_NAME, ClientEnd::new(p))?;
    ServiceProviderRequestStream::from_channel(chan)
        .map_err(Into::<Error>::into)
        .try_for_each_concurrent(None, |req| async move {
            let ServiceProviderRequest::ConnectToService {
                chan,
                info: _,
                control_handle: _control_handle,
            } = req;
            tracing::info!(node_id = node_id.0, "Received service request for service");
            let chan =
                fidl::AsyncChannel::from_channel(chan).context("failed to make async channel")?;
            let mut stream = echo::EchoRequestStream::from_channel(chan);
            while let Some(echo::EchoRequest::EchoString { value, responder }) =
                stream.try_next().await.context("error running echo server")?
            {
                tracing::info!(node_id = node_id.0, "Received echo request for string {:?}", value);
                responder.send(value.as_ref().map(|s| &**s)).context("error sending response")?;
                tracing::info!(node_id = node_id.0, "echo response sent successfully");
            }
            Ok(())
        })
        .await?;
    drop(overnet);
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// Test driver

async fn run_echo_test(
    client: Arc<Overnet>,
    server: Arc<Overnet>,
    text: Option<&str>,
) -> Result<(), Error> {
    let server = Task::spawn(async move {
        let server_id = server.node_id();
        exec_server(server).await.unwrap();
        tracing::info!(server_id = server_id.0, "SERVER DONE");
    });
    let r = exec_client(client, text).await;
    drop(server);
    r
}
