// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use crate::future_help::log_errors;
use crate::labels::NodeId;
use crate::link::{LinkReceiver, LinkSender};
use crate::router::test_util::{run, test_security_context};
use crate::router::{Router, RouterOptions};
use anyhow::Error;
use fidl::HandleBased;
use fuchsia_async::Task;
use fuchsia_zircon_status as zx_status;
use futures::prelude::*;
use std::sync::{
    atomic::{AtomicU64, Ordering},
    Arc,
};

mod channel;
mod socket;

struct Service(futures::channel::mpsc::Sender<fidl::Channel>, &'static str);

impl fidl_fuchsia_overnet::ServiceProviderProxyInterface for Service {
    fn connect_to_service(
        &self,
        chan: fidl::Channel,
        _connection_info: fidl_fuchsia_overnet::ConnectionInfo,
    ) -> std::result::Result<(), fidl::Error> {
        let test_name = self.1;
        log::info!("{} got connection {:?}", test_name, chan);
        let mut sender = self.0.clone();
        Task::spawn(log_errors(
            async move {
                log::info!("{} sending the thing", test_name);
                sender.send(chan).await?;
                log::info!("{} sent the thing", test_name);
                Ok(())
            },
            format!("{} failed to send incoming request handle", test_name),
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
    test_name: &'static str,
}

async fn forward(sender: LinkSender, receiver: LinkReceiver) -> Result<(), Error> {
    let mut frame = [0u8; 2048];
    while let Some(n) = sender.next_send(&mut frame).await? {
        if let Err(e) = receiver.received_packet(&mut frame[..n]).await {
            log::warn!("Packet receive error: {:?}", e);
        }
    }
    Ok(())
}

async fn link(a: Arc<Router>, b: Arc<Router>) {
    let (ab_tx, ab_rx) = a.new_link(b.node_id()).await.unwrap();
    let (ba_tx, ba_rx) = b.new_link(a.node_id()).await.unwrap();
    futures::future::try_join(forward(ab_tx, ba_rx), forward(ba_tx, ab_rx)).await.map(drop).unwrap()
}

#[derive(Clone, Copy, Debug)]
enum Target {
    A,
    B,
    C,
}

const FIXTURE_INCREMENT: u64 = 100000;
static NEXT_FIXTURE_ID: AtomicU64 = AtomicU64::new(100 + FIXTURE_INCREMENT);

async fn fixture_thread(
    test_name: &'static str,
    tx_init: std::sync::mpsc::Sender<(fidl::Channel, fidl::Channel)>,
    rx_fin: futures::channel::oneshot::Receiver<()>,
) {
    let fixture_id = NEXT_FIXTURE_ID.fetch_add(FIXTURE_INCREMENT, Ordering::Relaxed);
    let router1 = Router::new(
        RouterOptions::new().set_node_id((fixture_id + 1).into()),
        Box::new(test_security_context()),
    )
    .unwrap();
    let router2 = Router::new(
        RouterOptions::new().set_node_id((fixture_id + 2).into()),
        Box::new(test_security_context()),
    )
    .unwrap();
    let router3 = Router::new(
        RouterOptions::new().set_node_id((fixture_id + 3).into()),
        Box::new(test_security_context()),
    )
    .unwrap();
    let _l1 = Task::spawn(link(router1.clone(), router2.clone()));
    let _l2 = Task::spawn(link(router2.clone(), router3.clone()));
    let _l3 = Task::spawn(link(router3.clone(), router1.clone()));
    let service = format!("distribute_handle_for_{}", test_name);
    let (send_handle, mut recv_handle) = futures::channel::mpsc::channel(1);
    log::info!("{} {} register 2", test_name, fixture_id);
    router2
        .service_map()
        .register_service(service.clone(), Box::new(Service(send_handle.clone(), test_name)))
        .await;
    log::info!("{} {} register 3", test_name, fixture_id);
    router3
        .service_map()
        .register_service(service.clone(), Box::new(Service(send_handle, test_name)))
        .await;
    // Wait til we can see both peers in the service map before progressing.
    let lpc = router1.new_list_peers_context();
    loop {
        let peers = lpc.list_peers().await.unwrap();
        let has_peer = |node_id: NodeId| {
            peers
                .iter()
                .find(|peer| {
                    node_id == peer.id.into()
                        && peer
                            .description
                            .services
                            .as_ref()
                            .unwrap()
                            .iter()
                            .find(|&s| *s == service)
                            .is_some()
                })
                .is_some()
        };
        if has_peer(router2.node_id()) && has_peer(router3.node_id()) {
            break;
        }
    }
    let (dist_a_to_b, dist_b) = fidl::Channel::create().unwrap();
    let (dist_a_to_c, dist_c) = fidl::Channel::create().unwrap();
    log::info!("{} {} connect 2", test_name, fixture_id);
    router1.connect_to_service(router2.node_id(), &service, dist_b).await.unwrap();
    log::info!("{} {} get 2", test_name, fixture_id);
    let dist_b = recv_handle.next().await.unwrap();
    log::info!("{} {} connect 3", test_name, fixture_id);
    router1.connect_to_service(router3.node_id(), &service, dist_c).await.unwrap();
    log::info!("{} {} get 3", test_name, fixture_id);
    let dist_c = recv_handle.next().await.unwrap();
    log::info!("{} {} send handles", test_name, fixture_id);
    tx_init.send((dist_a_to_b, dist_b)).unwrap();
    tx_init.send((dist_a_to_c, dist_c)).unwrap();
    rx_fin.await.unwrap();
}

impl Fixture {
    fn new(test_name: &'static str) -> Fixture {
        let (tx_init, rx_init) = std::sync::mpsc::channel();
        let (tx_fin, rx_fin) = futures::channel::oneshot::channel();
        std::thread::spawn(move || run(fixture_thread(test_name, tx_init, rx_fin)));
        let (dist_a_to_b, dist_b) = rx_init.recv().unwrap();
        let (dist_a_to_c, dist_c) = rx_init.recv().unwrap();
        Fixture { dist_a_to_b, dist_b, dist_a_to_c, dist_c, tx_fin: Some(tx_fin), test_name }
    }

    fn distribute_handle<H: HandleBased>(&self, h: H, target: Target) -> H {
        let h = h.into_handle();
        log::info!("{} distribute_handle: make {:?} on {:?}", self.test_name, h, target);
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
                    log::info!("{} distribute_handle: remote is {:?}", self.test_name, h);
                    return H::from_handle(h);
                }
                Err(zx_status::Status::SHOULD_WAIT) => {
                    std::thread::sleep(std::time::Duration::from_millis(100));
                    continue;
                }
                Err(e) => panic!("{} Unexpected error {:?}", self.test_name, e),
            }
        }
    }

    pub fn log(&mut self, msg: &str) {
        log::info!("{}: {}", self.test_name, msg);
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = self.tx_fin.take().unwrap().send(());
    }
}

pub(crate) fn run_test(f: impl FnOnce() + Send + 'static) {
    crate::router::test_util::init();
    let (tx, rx) = futures::channel::oneshot::channel();
    let t = std::thread::spawn(move || {
        f();
        let _ = tx.send(());
    });
    crate::router::test_util::run(async move { rx.await.unwrap() });
    t.join().unwrap();
}
