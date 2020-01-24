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
use std::collections::BTreeMap;

/// A type that can be converted into a fidl_fuchsia_overnet::Peer
#[derive(Debug, Clone)]
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

pub struct ServiceMap {
    local_services: Mutex<BTreeMap<String, Box<dyn ServiceProviderProxyInterface>>>,
    local_node_id: NodeId,
    local_service_list: Observable<Vec<String>>,
    list_peers: Observable<Vec<ListablePeer>>,
}

impl ServiceMap {
    pub fn new(local_node_id: NodeId) -> ServiceMap {
        ServiceMap {
            local_services: Mutex::new(BTreeMap::new()),
            local_node_id,
            local_service_list: Observable::new(Vec::new()),
            list_peers: Observable::new(Vec::new()),
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
            self.local_service_list.push(services.clone());
            self.update_list_peers(ListablePeer {
                node_id: self.local_node_id,
                is_self: true,
                services,
            });
        }
    }

    pub async fn update_node(&self, node_id: NodeId, services: Vec<String>) -> Result<(), Error> {
        if node_id == self.local_node_id {
            bail!("Attempt to set local services list");
        }
        self.update_list_peers(ListablePeer { node_id, is_self: false, services });
        Ok(())
    }

    fn update_list_peers(&self, update_peer: ListablePeer) {
        self.list_peers.edit(|peers| {
            for existing_peer in peers.iter_mut() {
                if existing_peer.node_id == update_peer.node_id {
                    *existing_peer = update_peer;
                    return;
                }
            }
            peers.push(update_peer);
        });
    }

    pub fn new_local_service_observer(&self) -> Observer<Vec<String>> {
        self.local_service_list.new_observer()
    }

    pub fn new_list_peers_observer(&self) -> Observer<Vec<ListablePeer>> {
        self.list_peers.new_observer()
    }
}
