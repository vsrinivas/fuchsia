// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use crate::future_help::log_errors;
use crate::labels::NodeId;
use crate::link::{LinkReceiver, LinkSender};
use crate::router::Router;
use crate::test_util::NodeIdGenerator;
use anyhow::Error;
use fidl::HandleBased;
use fuchsia_async::Task;
use futures::prelude::*;
use std::sync::{
    atomic::{AtomicU64, Ordering},
    Arc,
};

mod channel;
mod socket;

struct Service(futures::channel::mpsc::Sender<fidl::Channel>, String);

impl fidl_fuchsia_overnet::ServiceProviderProxyInterface for Service {
    fn connect_to_service(
        &self,
        chan: fidl::Channel,
        _connection_info: fidl_fuchsia_overnet::ConnectionInfo,
    ) -> std::result::Result<(), fidl::Error> {
        let test_name = self.1.clone();
        log::info!("{} got connection {:?}", test_name, chan);
        let mut sender = self.0.clone();
        Task::local(log_errors(
            async move {
                log::info!("{} sending the thing", test_name);
                sender.send(chan).await?;
                log::info!("{} sent the thing", test_name);
                Ok(())
            },
            format!("{} failed to send incoming request handle", self.1),
        ))
        .detach();
        Ok(())
    }
}

struct Fixture {
    dist_a_to_b: fidl::Channel,
    dist_b: fidl::AsyncChannel,
    dist_a_to_c: fidl::Channel,
    dist_c: fidl::AsyncChannel,
    test_name: String,
    _service_task: Task<()>,
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
    let (ab_tx, ab_rx) = a.new_link(b.node_id(), Box::new(|| None)).await.unwrap();
    let (ba_tx, ba_rx) = b.new_link(a.node_id(), Box::new(|| None)).await.unwrap();
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

impl Fixture {
    async fn new(mut node_id_gen: NodeIdGenerator) -> Fixture {
        let test_name = node_id_gen.test_desc();
        let fixture_id = NEXT_FIXTURE_ID.fetch_add(FIXTURE_INCREMENT, Ordering::Relaxed);
        let router1 = node_id_gen.new_router().unwrap();
        let router2 = node_id_gen.new_router().unwrap();
        let router3 = node_id_gen.new_router().unwrap();
        let l1 = link(router1.clone(), router2.clone());
        let l2 = link(router2.clone(), router3.clone());
        let l3 = link(router3.clone(), router1.clone());
        let service_task = Task::local(futures::future::join3(l1, l2, l3).map(drop));
        let service = format!("distribute_handle_for_{}", test_name);
        let (send_handle, mut recv_handle) = futures::channel::mpsc::channel(1);
        log::info!("{} {} register 2", test_name, fixture_id);
        router2
            .service_map()
            .register_service(
                service.clone(),
                Box::new(Service(send_handle.clone(), test_name.clone())),
            )
            .await;
        log::info!("{} {} register 3", test_name, fixture_id);
        router3
            .service_map()
            .register_service(service.clone(), Box::new(Service(send_handle, test_name.clone())))
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
        let dist_b = fidl::AsyncChannel::from_channel(dist_b).unwrap();
        let dist_c = fidl::AsyncChannel::from_channel(dist_c).unwrap();
        Fixture { dist_a_to_b, dist_b, dist_a_to_c, dist_c, test_name, _service_task: service_task }
    }

    async fn distribute_handle<H: HandleBased>(&self, h: H, target: Target) -> H {
        let h = h.into_handle();
        log::info!("{} distribute_handle: make {:?} on {:?}", self.test_name, h, target);
        let (dist_local, dist_remote) = match target {
            Target::A => return H::from_handle(h),
            Target::B => (&self.dist_a_to_b, &self.dist_b),
            Target::C => (&self.dist_a_to_c, &self.dist_c),
        };
        assert!(dist_local.write(&[], &mut vec![h]) == Ok(()));
        let mut msg = fidl::MessageBuf::new();
        dist_remote.recv_msg(&mut msg).await.unwrap();
        let (bytes, handles) = msg.split_mut();
        assert_eq!(bytes.len(), 0);
        assert_eq!(handles.len(), 1);
        let h = std::mem::replace(handles, vec![]).into_iter().next().unwrap();
        log::info!("{} distribute_handle: remote is {:?}", self.test_name, h);
        return H::from_handle(h);
    }

    pub fn log(&mut self, msg: &str) {
        log::info!("{}: {}", self.test_name, msg);
    }
}
