// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "1024"]

use {
    anyhow::{format_err, Context, Error},
    fuchsia_bluetooth::profile::{psm_from_protocol, Psm},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::Inspect,
    futures::{channel::mpsc, stream::StreamExt, FutureExt},
    profile_client::ProfileEvent,
    tracing::{error, info, warn},
};

mod metrics;
mod packets;
mod peer;
mod peer_manager;
mod profile;
mod service;
mod types;

#[cfg(test)]
mod tests;

use crate::{
    metrics::{MetricsNode, METRICS_NODE_NAME},
    peer_manager::PeerManager,
    profile::AvrcpService,
};

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["avrcp"]).expect("unable to initialize logger");

    // Begin searching for AVRCP target/controller SDP records on newly connected remote peers
    // and register our AVRCP service with the `bredr.Profile` service.
    let (profile_proxy, mut profile_client) =
        profile::connect_and_advertise().context("Unable to connect to BrEdr Profile Service")?;

    // Create a channel that peer manager will receive requests for peer controllers from the FIDL
    // service runner.
    // TODO(fxbug.dev/44330) handle back pressure correctly and reduce mpsc::channel buffer sizes.
    let (client_sender, mut service_request_receiver) = mpsc::channel(512);

    let mut fs = ServiceFs::new();

    let inspect = inspect::Inspector::new();
    inspect_runtime::serve(&inspect, &mut fs)?;

    let mut peer_manager = PeerManager::new(profile_proxy);
    if let Err(e) = peer_manager.iattach(inspect.root(), "peers") {
        warn!("Failed to attach to inspect: {:?}", e);
    }
    let mut metrics_node = MetricsNode::default();
    if let Err(e) = metrics_node.iattach(inspect.root(), METRICS_NODE_NAME) {
        warn!("Failed to attach to inspect metrics: {:?}", e);
    }
    peer_manager.set_metrics_node(metrics_node);

    let mut service_fut = service::run_services(fs, client_sender)
        .expect("Unable to start AVRCP FIDL service")
        .fuse();

    loop {
        futures::select! {
            request = profile_client.next() => {
                let request = match request {
                    None => return Err(format_err!("BR/EDR Profile unexpectedly closed")),
                    Some(Err(e)) => return Err(format_err!("Profile client error: {:?}", e)),
                    Some(Ok(r)) => r,
                };
                match request {
                    ProfileEvent::PeerConnected { id, protocol, channel } => {
                        info!("Incoming connection request from {:?} with protocol: {:?}", id, protocol);
                        let protocol = protocol.iter().map(Into::into).collect();
                        match psm_from_protocol(&protocol) {
                            Some(Psm::AVCTP) => peer_manager.new_control_connection(&id, channel),
                            Some(Psm::AVCTP_BROWSE) => peer_manager.new_browse_connection(&id, channel),
                            _ => {
                                info!("Received connection over non-AVRCP protocol: {:?}", protocol);
                            },
                        }
                    },
                    ProfileEvent::SearchResult { id, protocol: _, attributes } => {
                        if let Some(service) = AvrcpService::from_attributes(attributes) {
                            info!("Service found on {:?}: {:?}", id, service);
                            peer_manager.services_found(&id, vec![service]);
                        }
                    },
                }
            }
            request = service_request_receiver.select_next_some() => {
                peer_manager.handle_service_request(request);
            },
            service_result = service_fut => {
                error!("Service task finished unexpectedly: {:?}", service_result);
                break;
            },
            complete => break,
        }
    }
    Ok(())
}
