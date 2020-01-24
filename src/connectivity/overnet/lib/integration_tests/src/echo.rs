// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Echo integration test for Fuchsia (much like the echo example - indeed the initial code came
//! from there, but self contained and more adaptable to different scenarios)

#![cfg(test)]

use {
    crate::Overnet,
    anyhow::{Context as _, Error},
    fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker},
    fidl_fidl_examples_echo as echo,
    fidl_fuchsia_overnet::{
        ServiceConsumerProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
        ServicePublisherProxyInterface,
    },
    futures::prelude::*,
    overnet_core::spawn,
    std::sync::Arc,
};

////////////////////////////////////////////////////////////////////////////////
// Test scenarios

#[test]
fn simple() -> Result<(), Error> {
    crate::run_async_test(async move {
        let client = Overnet::new()?;
        let server = Overnet::new()?;
        crate::connect(&client, &server)?;
        run_echo_test(client, server, Some("HELLO INTEGRATION TEST WORLD")).await
    })
}

#[test]
fn interspersed_log_messages() -> Result<(), Error> {
    crate::run_async_test(async move {
        let client = Overnet::new()?;
        let server = Overnet::new()?;
        crate::connect_with_interspersed_log_messages(&client, &server)?;
        run_echo_test(client, server, Some("HELLO INTEGRATION TEST WORLD")).await
    })
}

////////////////////////////////////////////////////////////////////////////////
// Client implementation

async fn exec_client(overnet: Arc<Overnet>, text: Option<&str>) -> Result<(), Error> {
    let svc = overnet.connect_as_service_consumer()?;
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
            let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
            svc.connect_to_service(&mut peer.id, echo::EchoMarker::NAME, s).unwrap();
            let proxy =
                fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
            let cli = echo::EchoProxy::new(proxy);
            log::trace!("Sending {:?} to {:?}", text, peer.id);
            assert_eq!(cli.echo_string(text).await.unwrap(), text.map(|s| s.to_string()));
            return Ok(());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Server implementation

async fn next_request(
    stream: &mut ServiceProviderRequestStream,
) -> Result<Option<ServiceProviderRequest>, Error> {
    log::trace!("Awaiting request");
    Ok(stream.try_next().await.context("error running service provider server")?)
}

async fn exec_server(overnet: Arc<Overnet>) -> Result<(), Error> {
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
    let mut stream = ServiceProviderRequestStream::from_channel(chan);
    overnet
        .connect_as_service_publisher()?
        .publish_service(echo::EchoMarker::NAME, ClientEnd::new(p))?;
    while let Some(ServiceProviderRequest::ConnectToService {
        chan,
        info: _,
        control_handle: _control_handle,
    }) = next_request(&mut stream).await?
    {
        log::trace!("Received service request for service");
        let chan =
            fidl::AsyncChannel::from_channel(chan).context("failed to make async channel")?;
        spawn(
            async move {
                let mut stream = echo::EchoRequestStream::from_channel(chan);
                while let Some(echo::EchoRequest::EchoString { value, responder }) =
                    stream.try_next().await.context("error running echo server")?
                {
                    log::trace!("Received echo request for string {:?}", value);
                    responder
                        .send(value.as_ref().map(|s| &**s))
                        .context("error sending response")?;
                    log::trace!("echo response sent successfully");
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| log::trace!("{:?}", e)),
        );
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// Test driver

async fn run_echo_test(
    client: Arc<Overnet>,
    server: Arc<Overnet>,
    text: Option<&str>,
) -> Result<(), Error> {
    spawn(async move { exec_server(server).await.unwrap() });
    exec_client(client, text).await
}
