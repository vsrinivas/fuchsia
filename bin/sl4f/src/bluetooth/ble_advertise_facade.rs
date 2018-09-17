// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, Fail, ResultExt};
use fidl_fuchsia_bluetooth_le::{AdvertisingData, PeripheralMarker, PeripheralProxy};
use fuchsia_app as app;
use fuchsia_async::temp::Either::{Left, Right};
use fuchsia_bluetooth::error::Error as BTError;
use fuchsia_syslog::macros::*;
use futures::future::ready as fready;
use futures::prelude::*;
use parking_lot::RwLock;
use std::sync::Arc;

// Sl4f-Constants and Ble advertising related functionality
use crate::bluetooth::constants::DEFAULT_BLE_ADV_INTERVAL_MS;
use crate::bluetooth::types::BleAdvertiseResponse;

// BleAdvertiseFacade: Starts and stops device BLE advertisement(s).
//
// This object is shared among all threads created by server.
//
// Use: TBD
#[derive(Debug)]
pub struct BleAdvertiseFacade {
    // adv_id: Advertisement ID of device, only one advertisement at a time.
    // TODO(NET-1290): Potentially scale up to a list/set of aid's for concurrent advertisement
    // tests.
    adv_id: Option<String>,

    // peripheral: PeripheralProxy used for Bluetooth Connections
    peripheral: Option<PeripheralProxy>,
}

impl BleAdvertiseFacade {
    pub fn new(peripheral_proxy: Option<PeripheralProxy>) -> Arc<RwLock<BleAdvertiseFacade>> {
        Arc::new(RwLock::new(BleAdvertiseFacade {
            adv_id: None,
            peripheral: peripheral_proxy,
        }))
    }

    // Set the advertisement ID if none exists already
    pub fn set_adv_id(facade: Arc<RwLock<BleAdvertiseFacade>>, aid: Option<String>) {
        if facade.read().adv_id.is_none() {
            facade.write().adv_id = aid
        } else {
            fx_log_warn!(tag: "set_adv_id", "Current aid: {:?}. Attempted aid: {:?}",
                facade.read().adv_id, aid);
        }
    }

    pub fn get_adv_id(&self) -> BleAdvertiseResponse {
        BleAdvertiseResponse::new(self.adv_id.clone())
    }

    pub fn print(&self) {
        fx_log_info!(tag: "print",
            "BleAdvertiseFacade: Adv_id: {:?}, Peripheral: {:?}",
            self.get_adv_id(),
            self.get_peripheral_proxy(),
        );
    }

    // Set the peripheral proxy only if none exists, otherwise, use existing
    pub fn set_peripheral_proxy(facade: Arc<RwLock<BleAdvertiseFacade>>) {
        let new_peripheral = match facade.read().peripheral.clone() {
            Some(p) => {
                fx_log_warn!(tag: "set_peripheral_proxy",
                    "Current peripheral: {:?}",
                    p,
                );
                Some(p)
            }
            None => {
                let peripheral_svc: PeripheralProxy = app::client::connect_to_service::<
                    PeripheralMarker,
                >().context("Failed to connect to BLE Peripheral service.")
                .unwrap();
                Some(peripheral_svc)
            }
        };

        facade.write().peripheral = new_peripheral
    }

    pub fn start_adv(
        facade: Arc<RwLock<BleAdvertiseFacade>>, adv_data: Option<AdvertisingData>,
        interval: Option<u32>,
    ) -> impl Future<Output = Result<(), Error>> {
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
        let ble_advertise_facade = facade.clone();
        BleAdvertiseFacade::set_peripheral_proxy(facade.clone());

        match &facade.read().peripheral {
            Some(p) => Right(
                p.start_advertising(&mut ad, None, intv, false)
                    .map_err(|e| e.context("failed to initiate advertise.").into())
                    .and_then(|(status, aid)| match status.error {
                        None => {
                            fx_log_info!(tag: "start_adv", "Started advertising id: {:?}", aid);
                            BleAdvertiseFacade::set_adv_id(ble_advertise_facade, aid.clone());
                            fready(Ok(()))
                        }
                        Some(e) => {
                            let err = BTError::from(*e);
                            fx_log_err!(tag: "start_adv", "Failed to start adveritising: {:?}", err);
                            fready(Err(err.into()))
                        }
                    }),
            ),
            None => {
                fx_log_err!(tag: "start_adv", "No peripheral created.");
                Left(fready(Err(
                    BTError::new("No peripheral proxy created.").into()
                )))
            }
        }
    }

    pub fn stop_adv(&self, aid: String) -> impl Future<Output = Result<(), Error>> {
        fx_log_info!(tag: "stop_adv", "stop_adv with aid: {:?}", aid);

        match &self.peripheral {
            Some(p) => Right(
                p.stop_advertising(&aid)
                    .map_err(|e| e.context("failed to stop advertise").into())
                    .and_then(|status| match status.error {
                        Some(e) => {
                            let err = BTError::from(*e);
                            fx_log_err!(tag: "stop_adv", "Failed to stop advertising: {:?}", err);
                            fready(Err(err.into()))
                        }
                        None => fready(Ok(())),
                    }),
            ),
            None => {
                fx_log_err!(tag: "stop_adv", "No peripheral proxy created!");
                Left(fready(Err(
                    BTError::new("No peripheral proxy created.").into()
                )))
            }
        }
    }

    pub fn get_peripheral_proxy(&self) -> &Option<PeripheralProxy> {
        &self.peripheral
    }

    pub fn cleanup_adv_id(facade: Arc<RwLock<BleAdvertiseFacade>>) {
        facade.write().adv_id = None
    }

    // Close peripheral proxy
    pub fn cleanup_peripheral_proxy(facade: Arc<RwLock<BleAdvertiseFacade>>) {
        facade.write().peripheral = None;
    }

    // Close both central and peripheral proxies
    pub fn cleanup(facade: Arc<RwLock<BleAdvertiseFacade>>) {
        BleAdvertiseFacade::cleanup_adv_id(facade.clone());
        BleAdvertiseFacade::cleanup_peripheral_proxy(facade.clone());
    }
}
