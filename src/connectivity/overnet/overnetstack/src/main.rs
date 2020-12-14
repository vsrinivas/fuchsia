// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Overnet daemon for Fuchsia

#![deny(missing_docs)]

mod mdns;
mod serial;
mod udp;

use anyhow::Error;
use argh::FromArgs;
use fidl_fuchsia_overnet::{
    MeshControllerRequest, MeshControllerRequestStream, ServiceConsumerRequest,
    ServiceConsumerRequestStream, ServicePublisherRequest, ServicePublisherRequestStream,
};
use fuchsia_async::Task;
use fuchsia_component::server::ServiceFs;
use futures::channel::mpsc;
use futures::lock::Mutex;
use futures::prelude::*;
use overnet_core::{Router, RouterOptions, SimpleSecurityContext};
use std::sync::Arc;

#[derive(FromArgs)]
/// Overnet.
struct Opts {
    #[argh(switch)]
    /// publish mdns service
    mdns_publish: bool,

    #[argh(switch)]
    /// connect to mdns services
    mdns_connect: bool,

    #[argh(switch)]
    /// open a udp port
    udp: bool,

    #[argh(option, default = "\"debug\".to_string()")]
    /// add serial links
    /// Can be 'none', 'all', or a specific path to a serial device.
    serial: String,
}

async fn run_service_publisher_server(
    node: Arc<Router>,
    stream: ServicePublisherRequestStream,
) -> Result<(), Error> {
    stream
        .map_err(Into::into)
        .try_for_each_concurrent(None, |request| {
            let node = node.clone();
            async move {
                match request {
                    ServicePublisherRequest::PublishService { service_name, provider, .. } => {
                        node.register_service(service_name, provider).await
                    }
                }
            }
        })
        .await
}

async fn run_service_consumer_server(
    node: Arc<Router>,
    stream: ServiceConsumerRequestStream,
) -> Result<(), Error> {
    let list_peers_context = Arc::new(node.new_list_peers_context());
    stream
        .map_err(Into::into)
        .try_for_each_concurrent(None, |request| {
            let node = node.clone();
            let list_peers_context = list_peers_context.clone();
            async move {
                match request {
                    ServiceConsumerRequest::ListPeers { responder, .. } => {
                        let mut peers = list_peers_context.list_peers().await?;
                        responder.send(&mut peers.iter_mut())?;
                        Ok(())
                    }
                    ServiceConsumerRequest::ConnectToService {
                        node: node_id,
                        service_name,
                        chan,
                        ..
                    } => node.connect_to_service(node_id.id.into(), &service_name, chan).await,
                }
            }
        })
        .await
}

async fn run_mesh_controller_server(
    node: Arc<Router>,
    stream: MeshControllerRequestStream,
) -> Result<(), Error> {
    stream
        .map_err(Into::into)
        .try_for_each_concurrent(None, |request| {
            let node = node.clone();
            async move {
                match request {
                    MeshControllerRequest::AttachSocketLink { socket, options, .. } => {
                        if let Err(e) = node.run_socket_link(socket, options).await {
                            log::warn!("Socket link failed: {:?}", e);
                        }
                        Ok(())
                    }
                }
            }
        })
        .await
}

/// Runs some part of overnet.
/// Subsystem's are allowed to fail without overnetstack failing.
async fn maybe_run_subsystem(
    cond: bool,
    name: &str,
    run: impl Future<Output = Result<(), Error>>,
) -> Result<(), Error> {
    if cond {
        if let Err(e) = run.await {
            log::warn!("{} subsystem failed and will be restarted: {:?}", name, e);
        } else {
            log::info!("{} subsystem completed successfully", name);
        }
    }
    Ok(())
}

enum IncomingService {
    ServiceConsumer(ServiceConsumerRequestStream),
    ServicePublisher(ServicePublisherRequestStream),
    MeshController(MeshControllerRequestStream),
    // ... more services here
}

#[fuchsia::component]
async fn main(opt: Opts) -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    let mut svc_dir = fs.dir("svc");
    svc_dir.add_fidl_service(IncomingService::ServiceConsumer);
    svc_dir.add_fidl_service(IncomingService::ServicePublisher);
    svc_dir.add_fidl_service(IncomingService::MeshController);

    fs.take_and_serve_directory_handle()?;

    let node = Router::new(
        RouterOptions::new()
            .export_diagnostics(fidl_fuchsia_overnet_protocol::Implementation::OvernetStack),
        Box::new(SimpleSecurityContext {
            node_cert: "/pkg/data/cert.crt",
            node_private_key: "/pkg/data/cert.key",
            root_cert: "/pkg/data/rootca.crt",
        }),
    )?;

    let (tx_new_conn, rx_new_conn) = mpsc::channel(1);
    let (tx_addr, rx_addr) = mpsc::channel(1);
    let mdns_publisher = &Mutex::new(None);
    futures::future::try_join5(
        // Serial comms
        maybe_run_subsystem(
            true,
            "Serial",
            crate::serial::run_serial_link_handlers(Arc::downgrade(&node), opt.serial),
        ),
        // UDP comms
        maybe_run_subsystem(
            opt.udp,
            "UDP",
            crate::udp::run_udp(Arc::downgrade(&node), rx_new_conn, tx_addr),
        ),
        // MDNS
        maybe_run_subsystem(
            opt.mdns_publish,
            "MDNS-publish",
            rx_addr.map(|a| a.port()).map(Ok).try_for_each(|p| async move {
                *mdns_publisher.lock().await = Some(Task::spawn(crate::mdns::publish(p)));
                Ok(())
            }),
        ),
        maybe_run_subsystem(
            opt.mdns_connect,
            "MDNS-subscribe",
            crate::mdns::subscribe(tx_new_conn),
        ),
        // Service loop
        fs.for_each_concurrent(None, move |svcreq| match svcreq {
            IncomingService::MeshController(stream) => {
                run_mesh_controller_server(node.clone(), stream)
                    .unwrap_or_else(|e| log::trace!("{:?}", e))
                    .boxed_local()
            }
            IncomingService::ServicePublisher(stream) => {
                run_service_publisher_server(node.clone(), stream)
                    .unwrap_or_else(|e| log::trace!("{:?}", e))
                    .boxed_local()
            }
            IncomingService::ServiceConsumer(stream) => {
                run_service_consumer_server(node.clone(), stream)
                    .unwrap_or_else(|e| log::trace!("{:?}", e))
                    .boxed_local()
            }
        })
        .map(Ok),
    )
    .await
    .map(drop)
}
