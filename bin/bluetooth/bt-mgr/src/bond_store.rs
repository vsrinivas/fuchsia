// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bond_defs::{BondMap, VecBondingData};
use fuchsia_syslog::{fx_log, fx_log_info};
use failure::Error;
use fidl;
use fidl_fuchsia_bluetooth_control::{BondingData, BondingEvent, BondingProxy};
use parking_lot::RwLock;
use serde_json;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::{Read, Write};
use std::sync::Arc;

static BT_BONDS_DATA: &'static str = "/data/bonds.json";

#[derive(Debug)]
pub struct BondStore {
    bonds: BondMap,
    bond_store: File,
}

impl BondStore {
    pub fn load_store() -> Result<Self, Error> {
        let mut bond_store = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .open(BT_BONDS_DATA)?;

        let mut contents = String::new();
        bond_store
            .read_to_string(&mut contents)
            .expect("The bond storage file is corrupted");
        let bonds: BondMap = match serde_json::from_str(contents.as_str()) {
            Ok(parsed) => parsed,
            Err(e) =>  {
                fx_log_info!("Could not parse contents of bond file {:?}", e);
                BondMap::new()
            }
        };

        Ok(BondStore { bonds, bond_store })
    }

    pub fn remove(&mut self, local_id: String) -> Result<(), Error> {
        self.bonds().inner_mut().remove(local_id.as_str());
        self.save_state()
    }

    pub fn add(&mut self, local_id: String, bond_data: BondingData) -> Result<(), Error> {
        {
            let val = self
                .bonds()
                .inner_mut()
                .entry(local_id)
                .or_insert(VecBondingData { inner: vec![] });
            val.inner.push(bond_data);
        }
        self.save_state()
    }

    pub fn bonds(&mut self) -> &mut BondMap {
        &mut self.bonds
    }

    pub fn save_state(&mut self) -> Result<(), Error> {
        let json = serde_json::to_string_pretty(&self.bonds)?;
        self.bond_store.write_all(json.as_bytes())?;
        self.bond_store.sync_data()?;
        Ok(())
    }
}

/// Load the devices from the bond store and send them over the BondingProxy when bt-mgr is first
/// started. Fidl errors should only occur if the channel is dropped
pub async fn restore_bonded_devices(bond_store: Arc<RwLock<BondStore>>, bond_svc: BondingProxy) -> Result<(), fidl::Error> {
    let mut bond_store = bond_store.write();
    for (mut bond_key, mut bond_data) in bond_store.bonds().iter_mut() {
        await!(bond_svc.add_bonded_devices(bond_key, &mut bond_data.iter_mut()))?;
    }
    Ok(())
}

/// Match on bonding events sent by bt-gap and manipulate the bond store.
pub fn bond_event(bond_store: Arc<RwLock<BondStore>>, evt: BondingEvent) -> Result<(), Error> {
    match evt {
        BondingEvent::OnNewBondingData { local_id, data } => {
            fx_log_info!("Adding a new bond: {}", local_id);
            let mut bond_store = bond_store.write();
            bond_store.add(local_id, data)
        }
        BondingEvent::OnDeleteBond { local_id } => {
            fx_log_info!("Removing a bond: {}", local_id);
            let mut bond_store = bond_store.write();
            bond_store.remove(local_id)
        }
    }
}
