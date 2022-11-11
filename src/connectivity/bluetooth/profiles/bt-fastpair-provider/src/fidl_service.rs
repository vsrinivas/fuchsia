// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_bluetooth_fastpair::{ProviderMarker, ProviderRequest, ProviderRequestStream};
use fidl_fuchsia_bluetooth_sys::{PairingRequest, PairingRequestStream};
use fuchsia_component::server::{ServiceFs, ServiceObj};
use futures::future::BoxFuture;
use futures::{channel::mpsc::Sender, FutureExt, SinkExt, StreamExt};
use tracing::{trace, warn};

use crate::pairing::PairingArgs;
use crate::provider::ServiceRequest;

/// The maximum number of FIDL service client connections that will be serviced concurrently.
const MAX_CONCURRENT_CONNECTIONS: usize = 10;

/// All FIDL services that are exposed by this component's ServiceFs.
pub enum Service {
    Pairing(PairingRequestStream),
    FastPair(ProviderRequestStream),
}

fn handle_pairing_client_connection(
    mut stream: PairingRequestStream,
    mut sender: Sender<ServiceRequest>,
) -> BoxFuture<'static, ()> {
    trace!("New sys.Pairing client connection");
    async move {
        while let Some(request) = stream.next().await {
            match request {
                Ok(PairingRequest::SetPairingDelegate { input, output, delegate, .. }) => {
                    match delegate.into_proxy() {
                        Ok(delegate) => {
                            let client = PairingArgs { input, output, delegate };
                            if let Err(e) = sender.send(ServiceRequest::Pairing(client)).await {
                                warn!(
                                    "Couldn't relay SetPairingDelegate request to component: {:?}",
                                    e
                                );
                            }
                        }
                        Err(e) => warn!("Couldn't obtain PairingDelegate client: {:?}", e),
                    }
                }
                Err(e) => {
                    warn!("Error in sys.Pairing stream: {:?}. Closing connection", e);
                    break;
                }
            }
        }
        trace!("sys.Pairing connection finished");
    }
    .boxed()
}

fn handle_fastpair_client_connection(
    mut stream: ProviderRequestStream,
    mut sender: Sender<ServiceRequest>,
) -> BoxFuture<'static, ()> {
    trace!("New {} client connection", ProviderMarker::PROTOCOL_NAME);
    async move {
        while let Some(request) = stream.next().await {
            match request {
                Ok(ProviderRequest::Enable { watcher, responder }) => match watcher.into_proxy() {
                    Ok(watcher) => {
                        if let Err(e) =
                            sender.send(ServiceRequest::EnableFastPair { watcher, responder }).await
                        {
                            warn!("Couldn't relay Fast Pair enable request to component: {:?}", e);
                        }
                    }
                    Err(e) => warn!("Couldn't obtain FastPair Provider client: {:?}", e),
                },
                Err(e) => {
                    warn!("Error in fastpair.Provider stream: {:?}. Closing connection", e);
                    break;
                }
            }
        }
        trace!("{} connection finished", ProviderMarker::PROTOCOL_NAME);
    }
    .boxed()
}

pub async fn run_services(
    mut fs: ServiceFs<ServiceObj<'_, Service>>,
    request_sender: Sender<ServiceRequest>,
) -> Result<(), Error> {
    let _ = fs.dir("svc").add_fidl_service(Service::Pairing).add_fidl_service(Service::FastPair);
    let _ = fs.take_and_serve_directory_handle().context("Failed to serve ServiceFs directory")?;
    fs.for_each_concurrent(MAX_CONCURRENT_CONNECTIONS, move |connection| match connection {
        Service::Pairing(stream) => {
            handle_pairing_client_connection(stream, request_sender.clone())
        }
        Service::FastPair(stream) => {
            handle_fastpair_client_connection(stream, request_sender.clone())
        }
    })
    .await;
    Ok(())
}
