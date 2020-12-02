// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mode_management::iface_manager_api::IfaceManagerApi,
    fidl_fuchsia_wlan_device_service as wlan_service, fidl_fuchsia_wlan_service as legacy,
    fidl_fuchsia_wlan_sme as fidl_sme,
    futures::lock::Mutex as FutureMutex,
    std::sync::{Arc, Mutex},
};

pub mod deprecated_configuration;
pub mod device;
pub mod known_ess_store;
pub mod shim;

#[derive(Clone)]
pub(crate) struct Iface {
    pub service: wlan_service::DeviceServiceProxy,
    pub iface_manager: Arc<FutureMutex<dyn IfaceManagerApi + Send>>,
    pub sme: fidl_sme::ClientSmeProxy,
    pub iface_id: u16,
}

#[derive(Clone)]
pub(crate) struct IfaceRef(Arc<Mutex<Option<Iface>>>);
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
    pub fn get(&self) -> Result<Iface, legacy::Error> {
        self.0.lock().unwrap().clone().ok_or_else(|| legacy::Error {
            code: legacy::ErrCode::NotFound,
            description: "No wireless interface found".to_string(),
        })
    }
}
