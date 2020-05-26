// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use crate::future_help::log_errors;
use crate::link::Link;
use crate::router::test_util::{run, test_router_options};
use crate::router::Router;
use crate::runtime::Task;
use anyhow::Error;
use fidl::HandleBased;
use fuchsia_zircon_status as zx_status;
use futures::prelude::*;
use std::sync::Arc;

mod channel;
mod socket;

struct Service(futures::channel::mpsc::Sender<fidl::Channel>);

impl fidl_fuchsia_overnet::ServiceProviderProxyInterface for Service {
    fn connect_to_service(
        &self,
        chan: fidl::Channel,
        _connection_info: fidl_fuchsia_overnet::ConnectionInfo,
    ) -> std::result::Result<(), fidl::Error> {
        log::info!("got connection {:?}", chan);
        let mut sender = self.0.clone();
        Task::spawn(log_errors(
            async move {
                log::info!("sending the thing");
                sender.send(chan).await?;
                log::info!("sent the thing");
                Ok(())
            },
            "failed to send incoming request handle",
        ))
        .detach();
        Ok(())
    }
}

struct Fixture {
    dist_a_to_b: fidl::Channel,
    dist_b: fidl::Channel,
    dist_a_to_c: fidl::Channel,
    dist_c: fidl::Channel,
    tx_fin: Option<futures::channel::oneshot::Sender<()>>,
}

async fn forward(sender: Arc<Link>, receiver: Arc<Link>) -> Result<(), Error> {
    assert_eq!(sender.peer_node_id(), receiver.own_node_id());
    assert_eq!(sender.own_node_id(), receiver.peer_node_id());
    let mut frame = [0u8; 2048];
    while let Some(n) = sender.next_send(&mut frame).await? {
        receiver.received_packet(&mut frame[..n]).await?;
    }
    Ok(())
}

async fn link(a: Arc<Router>, b: Arc<Router>) {
    let ab = a.new_link(b.node_id()).await.unwrap();
    let ba = b.new_link(a.node_id()).await.unwrap();
    futures::future::try_join(forward(ab.clone(), ba.clone()), forward(ba, ab))
        .await
        .map(drop)
        .unwrap()
}

#[derive(Clone, Copy, Debug)]
enum Target {
    A,
    B,
    C,
}

async fn fixture_thread(
    tx_init: std::sync::mpsc::Sender<(fidl::Channel, fidl::Channel)>,
    rx_fin: futures::channel::oneshot::Receiver<()>,
) {
    let router1 = Router::new(test_router_options()).unwrap();
    let router2 = Router::new(test_router_options()).unwrap();
    let router3 = Router::new(test_router_options()).unwrap();
    let _l1 = Task::spawn(link(router1.clone(), router2.clone()));
    let _l2 = Task::spawn(link(router2.clone(), router3.clone()));
    let _l3 = Task::spawn(link(router3.clone(), router1.clone()));
    const SERVICE: &'static str = "distribute_handle";
    let (send_handle, mut recv_handle) = futures::channel::mpsc::channel(1);
    log::info!("register 2");
    router2
        .service_map()
        .register_service(SERVICE.to_string(), Box::new(Service(send_handle.clone())))
        .await;
    log::info!("register 3");
    router3
        .service_map()
        .register_service(SERVICE.to_string(), Box::new(Service(send_handle)))
        .await;
    let (dist_a_to_b, dist_b) = fidl::Channel::create().unwrap();
    let (dist_a_to_c, dist_c) = fidl::Channel::create().unwrap();
    log::info!("connect 2");
    router1.connect_to_service(router2.node_id(), SERVICE, dist_b).await.unwrap();
    log::info!("get 2");
    let dist_b = recv_handle.next().await.unwrap();
    log::info!("connect 3");
    router1.connect_to_service(router3.node_id(), SERVICE, dist_c).await.unwrap();
    log::info!("get 3");
    let dist_c = recv_handle.next().await.unwrap();
    log::info!("send handles");
    tx_init.send((dist_a_to_b, dist_b)).unwrap();
    tx_init.send((dist_a_to_c, dist_c)).unwrap();
    rx_fin.await.unwrap();
}

impl Fixture {
    fn new() -> Fixture {
        let (tx_init, rx_init) = std::sync::mpsc::channel();
        let (tx_fin, rx_fin) = futures::channel::oneshot::channel();
        std::thread::spawn(move || run(|| fixture_thread(tx_init, rx_fin)));
        let (dist_a_to_b, dist_b) = rx_init.recv().unwrap();
        let (dist_a_to_c, dist_c) = rx_init.recv().unwrap();
        Fixture { dist_a_to_b, dist_b, dist_a_to_c, dist_c, tx_fin: Some(tx_fin) }
    }

    fn distribute_handle<H: HandleBased>(&self, h: H, target: Target) -> H {
        let h = h.into_handle();
        log::info!("distribute_handle: make {:?} on {:?}", h, target);
        let (dist_local, dist_remote) = match target {
            Target::A => return H::from_handle(h),
            Target::B => (&self.dist_a_to_b, &self.dist_b),
            Target::C => (&self.dist_a_to_c, &self.dist_c),
        };
        assert!(dist_local.write(&[], &mut vec![h]) == Ok(()));
        loop {
            let (mut bytes, mut handles) = (Vec::new(), Vec::new());
            match dist_remote.read_split(&mut bytes, &mut handles) {
                Ok(()) => {
                    assert_eq!(bytes, vec![]);
                    assert_eq!(handles.len(), 1);
                    let h = handles.into_iter().next().unwrap();
                    log::info!("distribute_handle: remote is {:?}", h);
                    return H::from_handle(h);
                }
                Err(zx_status::Status::SHOULD_WAIT) => {
                    std::thread::sleep(std::time::Duration::from_millis(10));
                    continue;
                }
                Err(e) => panic!("Unexpected error {:?}", e),
            }
        }
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = self.tx_fin.take().unwrap().send(());
    }
}
