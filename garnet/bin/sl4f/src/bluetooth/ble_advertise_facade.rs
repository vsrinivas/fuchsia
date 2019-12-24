// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_bluetooth_le::{
    AdvertisingHandleMarker, AdvertisingParameters, PeripheralMarker, PeripheralProxy,
};
use fuchsia_component as app;
use fuchsia_syslog::macros::*;
use parking_lot::RwLock;

// Sl4f-Constants and Ble advertising related functionality
use crate::common_utils::error::Sl4fError;

#[derive(Debug)]
struct InnerBleAdvertiseFacade {
    /// Advertisement ID of device, only one advertisement at a time.
    // TODO(NET-1290): Potentially scale up to storing multiple AdvertisingHandles. We may want to
    // generate unique identifiers for each advertisement and to allow RPC clients to manage them.
    adv_handle: Option<ClientEnd<AdvertisingHandleMarker>>,

    ///PeripheralProxy used for Bluetooth Connections
    peripheral: Option<PeripheralProxy>,
}

/// Starts and stops device BLE advertisement(s).
/// Note this object is shared among all threads created by server.
#[derive(Debug)]
pub struct BleAdvertiseFacade {
    inner: RwLock<InnerBleAdvertiseFacade>,
}

impl BleAdvertiseFacade {
    pub fn new() -> BleAdvertiseFacade {
        BleAdvertiseFacade {
            inner: RwLock::new(InnerBleAdvertiseFacade { adv_handle: None, peripheral: None }),
        }
    }

    // Store a new advertising handle.
    fn set_adv_handle(&self, adv_handle: Option<ClientEnd<AdvertisingHandleMarker>>) {
        if adv_handle.is_some() {
            fx_log_info!(tag: "set_adv_handle", "Assigned new advertising handle");
        } else {
            fx_log_info!(tag: "set_adv_handle", "Cleared advertising handle");
        }
        self.inner.write().adv_handle = adv_handle;
    }

    pub fn print(&self) {
        let adv_status = match &self.inner.read().adv_handle {
            Some(_) => "Valid",
            None => "None",
        };
        fx_log_info!(tag: "print",
            "BleAdvertiseFacade: Adv Status: {}, Peripheral: {:?}",
            adv_status,
            self.get_peripheral_proxy(),
        );
    }

    // Set the peripheral proxy only if none exists, otherwise, use existing
    pub fn set_peripheral_proxy(&self) {
        let new_peripheral = match self.inner.read().peripheral.clone() {
            Some(p) => {
                fx_log_warn!(tag: "set_peripheral_proxy",
                    "Current peripheral: {:?}",
                    p,
                );
                Some(p)
            }
            None => {
                let peripheral_svc: PeripheralProxy =
                    app::client::connect_to_service::<PeripheralMarker>()
                        .context("Failed to connect to BLE Peripheral service.")
                        .unwrap();
                Some(peripheral_svc)
            }
        };

        self.inner.write().peripheral = new_peripheral
    }

    pub async fn start_adv(&self, parameters: AdvertisingParameters) -> Result<(), Error> {
        // Create peripheral proxy if necessary
        self.set_peripheral_proxy();
        let periph = &self.inner.read().peripheral.clone();
        match &periph {
            Some(p) => {
                let (handle, handle_remote) = create_endpoints::<AdvertisingHandleMarker>()?;
                let result = p.start_advertising(parameters, handle_remote).await?;
                if let Err(err) = result {
                    fx_log_err!(tag: "start_adv", "Failed to start adveritising: {:?}", err);
                    return Err(Sl4fError::new(&format!("{:?}", err)).into());
                }
                fx_log_info!(tag: "start_adv", "Started advertising");
                self.set_adv_handle(Some(handle));
                Ok(())
            }
            None => {
                fx_log_err!(tag: "start_adv", "No peripheral created.");
                return Err(format_err!("No peripheral proxy created."));
            }
        }
    }

    pub fn stop_adv(&self) {
        fx_log_info!(tag: "stop_adv", "Stop advertising");
        self.set_adv_handle(None);
    }

    pub fn get_peripheral_proxy(&self) -> Option<PeripheralProxy> {
        self.inner.read().peripheral.clone()
    }

    pub fn cleanup_advertisements(&self) {
        self.set_adv_handle(None);
    }

    // Close peripheral proxy
    pub fn cleanup_peripheral_proxy(&self) {
        self.inner.write().peripheral = None;
    }

    // Close both central and peripheral proxies
    pub fn cleanup(&self) {
        self.cleanup_advertisements();
        self.cleanup_peripheral_proxy();
    }
}
