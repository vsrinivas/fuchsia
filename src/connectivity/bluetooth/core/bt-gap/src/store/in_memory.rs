// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    fuchsia_bluetooth::types::{Address, BondingData, HostData, PeerId},
    std::collections::HashMap,
};

use crate::store::stash::Request;

#[derive(Default)]
pub(crate) struct InMemoryStore {
    host_data: HashMap<Address, HostData>,
    bond_data: HashMap<Address, Vec<BondingData>>,
}

impl InMemoryStore {
    fn store_host(&mut self, address: Address, host: HostData) {
        self.host_data.insert(address, host);
    }
    fn get_host(&self, address: Address) -> Option<HostData> {
        self.host_data.get(&address).cloned()
    }
    fn store_bond(&mut self, bond: BondingData) {
        self.bond_data.entry(bond.local_address).or_insert(vec![]).push(bond);
    }
    fn list_bonds(&self, address: Address) -> Option<Vec<BondingData>> {
        self.bond_data.get(&address).cloned()
    }
    fn rm_peer(&mut self, peer: PeerId) {
        for bonds in &mut self.bond_data.values_mut() {
            bonds.retain(|bond| bond.identifier != peer)
        }
    }

    pub(crate) fn handle_request(&mut self, req: Request) {
        match req {
            Request::StoreBonds(bonds, responder) => {
                bonds.into_iter().for_each(|b| self.store_bond(b));
                let _ = responder.send(Ok(()));
            }
            Request::RmPeer(peer_id, responder) => {
                self.rm_peer(peer_id);
                let _ = responder.send(Ok(()));
            }
            Request::StoreHostData(address, host_data, responder) => {
                self.store_host(address, host_data);
                let _ = responder.send(Ok(()));
            }
            Request::GetHostData(address, responder) => {
                let _ = responder.send(self.get_host(address));
            }
            Request::ListBonds(address, responder) => {
                let _ = responder.send(self.list_bonds(address));
            }
        }
    }
}
