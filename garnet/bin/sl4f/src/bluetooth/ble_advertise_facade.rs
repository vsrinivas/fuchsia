// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, Error, ResultExt};
use fidl_fuchsia_bluetooth_le::{AdvertisingData, PeripheralMarker, PeripheralProxy};
use fuchsia_bluetooth::error::Error as BTError;
use fuchsia_component as app;
use fuchsia_syslog::macros::*;
use parking_lot::RwLock;

// Sl4f-Constants and Ble advertising related functionality
use crate::bluetooth::constants::DEFAULT_BLE_ADV_INTERVAL_MS;
use crate::bluetooth::types::BleAdvertiseResponse;

#[derive(Debug)]
struct InnerBleAdvertiseFacade {
    /// Advertisement ID of device, only one advertisement at a time.
    // TODO(NET-1290): Potentially scale up to a list/set of adv_id's for concurrent advertisement
    // tests.
    adv_id: Option<String>,

    ///PeripheralProxy used for Bluetooth Connections
    peripheral: Option<PeripheralProxy>,
}

/// Starts and stops device BLE advertisement(s).
//
/// Note this object is shared among all threads created by server.
//
#[derive(Debug)]
pub struct BleAdvertiseFacade {
    inner: RwLock<InnerBleAdvertiseFacade>,
}

impl BleAdvertiseFacade {
    pub fn new() -> BleAdvertiseFacade {
        BleAdvertiseFacade {
            inner: RwLock::new(InnerBleAdvertiseFacade { adv_id: None, peripheral: None }),
        }
    }

    // Set the advertisement ID if none exists already
    pub fn set_adv_id(&self, adv_id: Option<String>) {
        self.inner.write().adv_id = adv_id.clone();
        fx_log_info!(tag: "set_adv_id", "Advertisement ID set to: {:?}", adv_id)
    }

    pub fn get_adv_id(&self) -> BleAdvertiseResponse {
        BleAdvertiseResponse::new(self.inner.read().adv_id.clone())
    }

    pub fn print(&self) {
        fx_log_info!(tag: "print",
            "BleAdvertiseFacade: Adv_id: {:?}, Peripheral: {:?}",
            self.get_adv_id(),
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

    pub async fn start_adv(
        &self,
        adv_data: Option<AdvertisingData>,
        interval: Option<u32>,
        connectable: bool,
    ) -> Result<(), Error> {
        // Default interval (ms) to 1 second
        let intv: u32 = interval.unwrap_or(DEFAULT_BLE_ADV_INTERVAL_MS);

        let mut ad = match adv_data {
            Some(ad) => ad,
            None => AdvertisingData {
                name: None,
                tx_power_level: None,
                appearance: None,
                service_uuids: None,
                service_data: None,
                manufacturer_specific_data: None,
                solicited_service_uuids: None,
                uris: None,
            },
        };

        // Create peripheral proxy if necessary
        self.set_peripheral_proxy();
        let periph = &self.inner.read().peripheral.clone();
        match &periph {
            Some(p) => {
                let (status, adv_id) =
                    await!(p.start_advertising(&mut ad, None, connectable, intv, false))?;
                match status.error {
                    None => {
                        fx_log_info!(tag: "start_adv", "Started advertising id: {:?}", adv_id);
                        self.set_adv_id(adv_id.clone());
                        Ok(())
                    }
                    Some(e) => {
                        let err = BTError::from(*e);
                        fx_log_err!(tag: "start_adv", "Failed to start adveritising: {:?}", err);
                        Err(err.into())
                    }
                }
            }
            None => {
                fx_log_err!(tag: "start_adv", "No peripheral created.");
                bail!("No peripheral proxy created.")
            }
        }
    }

    pub async fn stop_adv(&self, adv_id: String) -> Result<(), Error> {
        fx_log_info!(tag: "stop_adv", "stop_adv with adv_id: {:?}", adv_id);

        let periph = &self.inner.read().peripheral.clone();
        match &periph {
            Some(p) => {
                await!(p.stop_advertising(&adv_id))?;
                self.set_adv_id(None);
                Ok(())
            }
            None => {
                fx_log_err!(tag: "stop_adv", "No peripheral proxy created!");
                bail!("No peripheral proxy created.")
            }
        }
    }

    pub fn get_peripheral_proxy(&self) -> Option<PeripheralProxy> {
        self.inner.read().peripheral.clone()
    }

    pub fn cleanup_adv_id(&self) {
        self.inner.write().adv_id = None
    }

    // Close peripheral proxy
    pub fn cleanup_peripheral_proxy(&self) {
        self.inner.write().peripheral = None;
    }

    // Close both central and peripheral proxies
    pub fn cleanup(&self) {
        self.cleanup_adv_id();
        self.cleanup_peripheral_proxy();
    }
}
