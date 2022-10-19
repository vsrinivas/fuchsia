// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_wlan_sme as fidl_sme,
    std::sync::{Arc, Mutex},
};

pub mod deprecated_client;
pub mod deprecated_configuration;

#[derive(Clone)]
pub struct Iface {
    pub sme: fidl_sme::ClientSmeProxy,
    pub iface_id: u16,
}

#[derive(Clone)]
pub struct IfaceRef(Arc<Mutex<Option<Iface>>>);
impl IfaceRef {
    pub fn new() -> Self {
        IfaceRef(Arc::new(Mutex::new(None)))
    }
    pub fn set_if_empty(&self, iface: Iface) {
        let mut c = self.0.lock().unwrap();
        if c.is_none() {
            *c = Some(iface);
        }
    }
    pub fn remove_if_matching(&self, iface_id: u16) {
        let mut c = self.0.lock().unwrap();
        let same_id = match *c {
            Some(ref c) => c.iface_id == iface_id,
            None => false,
        };
        if same_id {
            *c = None;
        }
    }
    pub fn get(&self) -> Result<Iface, Error> {
        self.0
            .lock()
            .map_err(|e| format_err!("unable to lock IfaceRef: {:?}", e))?
            .clone()
            .ok_or_else(|| format_err!("no available client interfaces"))
    }
}
