// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    future_help::{Observable, Observer},
    labels::NodeId,
};
use anyhow::{bail, format_err, Error};
use fidl::Channel;
use fidl_fuchsia_overnet::{ConnectionInfo, ServiceProviderProxyInterface};
use futures::lock::Mutex;
use std::collections::{btree_map, BTreeMap};

/// A type that can be converted into a fidl_fuchsia_overnet::Peer
#[derive(Debug, Clone, PartialEq)]
pub struct ListablePeer {
    node_id: NodeId,
    is_self: bool,
    services: Vec<String>,
}

impl From<ListablePeer> for fidl_fuchsia_overnet::Peer {
    fn from(p: ListablePeer) -> fidl_fuchsia_overnet::Peer {
        fidl_fuchsia_overnet::Peer {
            id: p.node_id.into(),
            is_self: p.is_self,
            description: fidl_fuchsia_overnet_protocol::PeerDescription {
                services: Some(p.services),
            },
        }
    }
}

struct ListablePeerSet {
    listable_peers: Vec<ListablePeer>,
    peers_with_client_connection: BTreeMap<NodeId, usize>,
}

impl ListablePeerSet {
    fn publish(&self) -> Vec<ListablePeer> {
        let peers_with_client_connection = &self.peers_with_client_connection;
        self.listable_peers
            .iter()
            .filter(move |p| p.is_self || peers_with_client_connection.contains_key(&p.node_id))
            .cloned()
            .collect()
    }
}

pub struct ServiceMap {
    local_services: Mutex<BTreeMap<String, Box<dyn ServiceProviderProxyInterface>>>,
    local_node_id: NodeId,
    local_service_list: Observable<Vec<String>>,
    list_peers: Observable<Vec<ListablePeer>>,
    listable_peer_set: Mutex<ListablePeerSet>,
}

impl ServiceMap {
    pub fn new(local_node_id: NodeId) -> ServiceMap {
        let listable_peers =
            vec![ListablePeer { node_id: local_node_id, is_self: true, services: vec![] }];
        ServiceMap {
            local_services: Mutex::new(BTreeMap::new()),
            local_node_id,
            local_service_list: Observable::new_traced(Vec::new(), false),
            list_peers: Observable::new(listable_peers.clone()),
            listable_peer_set: Mutex::new(ListablePeerSet {
                listable_peers,
                peers_with_client_connection: std::iter::once((local_node_id, 1)).collect(),
            }),
        }
    }

    pub async fn connect(
        &self,
        service_name: &str,
        chan: Channel,
        connection_info: ConnectionInfo,
    ) -> Result<(), Error> {
        self.local_services
            .lock()
            .await
            .get(service_name)
            .ok_or_else(|| format_err!("Service not found: {}", service_name))?
            .connect_to_service(chan, connection_info)?;
        Ok(())
    }

    pub async fn register_service(
        &self,
        service_name: String,
        provider: Box<dyn ServiceProviderProxyInterface>,
    ) {
        log::trace!("Request register_service '{}'", service_name);
        let mut local_services = self.local_services.lock().await;
        if local_services.insert(service_name.clone(), provider).is_none() {
            log::trace!("Publish new service '{}'", service_name);
            let services: Vec<String> = local_services.keys().cloned().collect();
            drop(local_services);
            self.local_service_list.push(services.clone()).await;
        }
    }

    pub async fn update_node(&self, node_id: NodeId, services: Vec<String>) -> Result<(), Error> {
        if node_id == self.local_node_id {
            bail!("Attempt to set local services list");
        }
        self.update_list_peers(ListablePeer { node_id, is_self: false, services }).await;
        Ok(())
    }

    async fn update_list_peers(&self, update_peer: ListablePeer) {
        let mut lsp = self.listable_peer_set.lock().await;
        let peers = &mut lsp.listable_peers;
        for existing_peer in peers.iter_mut() {
            if existing_peer.node_id == update_peer.node_id {
                if *existing_peer == update_peer {
                    return;
                }
                *existing_peer = update_peer;
                self.list_peers.push(lsp.publish()).await;
                return;
            }
        }
        peers.push(update_peer);
        self.list_peers.push(lsp.publish()).await;
    }

    pub async fn add_client_connection(&self, peer_id: NodeId) {
        let mut lsp = self.listable_peer_set.lock().await;
        match lsp.peers_with_client_connection.entry(peer_id) {
            btree_map::Entry::Occupied(o) => *o.into_mut() += 1,
            btree_map::Entry::Vacant(v) => {
                v.insert(1);
                self.list_peers.push(lsp.publish()).await;
            }
        }
    }

    pub async fn remove_client_connection(&self, peer_id: NodeId) {
        let mut lsp = self.listable_peer_set.lock().await;
        match lsp.peers_with_client_connection.entry(peer_id) {
            btree_map::Entry::Occupied(mut o) => match *o.get() {
                0 => unreachable!(),
                1 => {
                    o.remove();
                    self.list_peers.push(lsp.publish()).await;
                }
                n => *o.get_mut() = n - 1,
            },
            btree_map::Entry::Vacant(_) => unreachable!(),
        }
    }

    pub fn new_local_service_observer(&self) -> Observer<Vec<String>> {
        self.local_service_list.new_observer()
    }

    pub fn new_list_peers_observer(&self) -> Observer<Vec<ListablePeer>> {
        self.list_peers.new_observer()
    }
}
