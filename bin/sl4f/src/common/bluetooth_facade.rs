// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bt::error::Error as BTError;
use common::constants::*;
use failure::{Error, Fail};
use fidl::encoding2::OutOfLine;
use fidl_ble::{AdvertisingData, PeripheralProxy, RemoteDevice};
use fidl_ble::{CentralEvent, CentralProxy, ScanFilter};
use futures::future;
use futures::future::ok as fok;
use futures::future::Either::{Left, Right};
use futures::prelude::*;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;

// BluetoothFacade: Stores Central and Peripheral proxies used for
// bluetooth scan and advertising requests.
//
// This object is shared among all threads created by server.
//
// Future plans: Object will store other common information like RemoteDevices
// found via scan, allowing for ease of state transfer between similar/related
// requests.
//
// Use: Create once per server instantiation. Calls to set_peripheral_proxy()
// and set_central_proxy() will update Facade object with proxy if no such proxy
// currently exists. If such a proxy exists, then update() will use pre-existing
// proxy.
#[derive(Debug)]
pub struct BluetoothFacade {
    // central: CentralProxy used for Bluetooth connections
    central: Option<CentralProxy>,

    // peripheral: PeripheralProxy used for Bluetooth Connections
    peripheral: Option<PeripheralProxy>,

    // devices: HashMap of key = device id and val = RemoteDevice structs
    devices: HashMap<String, RemoteDevice>,

    // adv_id: Advertisement ID of device, only one advertisement at a time.
    // TODO(aniramakri): Potentially scale up to a list/set of aid's for concurrent advertisement
    // tests.
    adv_id: Option<String>,
}

impl BluetoothFacade {
    pub fn new(
        central_proxy: Option<CentralProxy>, peripheral_proxy: Option<PeripheralProxy>,
    ) -> Arc<RwLock<BluetoothFacade>> {
        Arc::new(RwLock::new(BluetoothFacade {
            central: central_proxy,
            peripheral: peripheral_proxy,
            devices: HashMap::new(),
            adv_id: None,
        }))
    }

    // Set the peripheral proxy only if none exists, otherwise, use existing
    pub fn set_peripheral_proxy(&mut self, peripheral_proxy: PeripheralProxy) {
        let new_periph = match self.peripheral.clone() {
            Some(p) => {
                fx_log_warn!(tag: "set_peripheral_proxy",
                    "Current peripheral: {:?}. New peripheral: {:?}",
                    p, peripheral_proxy
                );
                Some(p)
            }
            None => Some(peripheral_proxy),
        };

        self.peripheral = new_periph
    }

    // Update the central proxy if none exists, otherwise raise error
    pub fn set_central_proxy(&mut self, central_proxy: CentralProxy) {
        let new_central = match self.central.clone() {
            Some(c) => {
                fx_log_warn!(tag: "set_central_proxy", "Current central: {:?}. New central: {:?}", c, central_proxy);
                Some(c)
            }
            None => Some(central_proxy),
        };

        self.central = new_central
    }

    // Update the devices dictionary with a discovered RemoteDevice
    pub fn update_devices(&mut self, id: String, device: RemoteDevice) {
        if self.devices.contains_key(&id) {
            fx_log_warn!(tag: "update_devices", "Already discovered: {:?}", id);
        } else {
            self.devices.insert(id, device);
        }
    }

    // Update the advertisement ID if none exists already
    pub fn update_adv_id(&mut self, aid: Option<String>) {
        if self.adv_id.is_none() {
            self.adv_id = aid
        } else {
            fx_log_warn!(tag: "update_adv_id", "Current aid: {:?}. Attempted aid: {:?}", self.adv_id, aid);
        }
    }

    // Given the devices accrued from scan, returns list of (id, name) devices
    // TODO(aniramakri): Return list of RemoteDevices (unsupported right now
    // because Clone() not implemented for RemoteDevice)
    pub fn get_devices(&self) -> Vec<(String, String)> {
        const EMPTY_DEVICE: &str = "";
        let mut devices = Vec::new();
        for val in self.devices.keys() {
            let name = match &self.devices[val].advertising_data {
                Some(adv) => adv.name.clone().unwrap_or(EMPTY_DEVICE.to_string()),
                None => EMPTY_DEVICE.to_string(),
            };
            devices.push((val.clone(), name));
        }

        devices
    }

    pub fn get_adv_id(&self) -> &Option<String> {
        &self.adv_id
    }

    // Return the central proxy
    pub fn get_central_proxy(&self) -> &Option<CentralProxy> {
        &self.central
    }

    pub fn get_peripheral_proxy(&self) -> &Option<PeripheralProxy> {
        &self.peripheral
    }

    /* Clean-up methods for BT Facade */
    // Close peripheral proxy
    pub fn cleanup_peripheral_proxy(&mut self) {
        self.peripheral = None;
    }

    pub fn cleanup_central_proxy(&mut self) {
        self.central = None;
    }

    pub fn cleanup_devices(&mut self) {
        self.devices.clear();
    }

    pub fn cleanup_adv_id(&mut self) {
        self.adv_id = None
    }

    pub fn cleanup_central(&mut self) {
        self.cleanup_central_proxy();
        self.cleanup_devices();
    }

    pub fn cleanup_peripheral(&mut self) {
        self.cleanup_peripheral_proxy();
        self.cleanup_adv_id();
    }

    // Close both central and peripheral proxies
    pub fn cleanup_facade(&mut self) {
        self.cleanup_peripheral();
        self.cleanup_central();
    }

    /* Print/Debug methods for BT Facade */
    pub fn print_facade(&self) {
        fx_log_info!(tag: "print_facade",
            "Facade: {:?}, {:?}, {:?}, {:?}",
            self.get_central_proxy(),
            self.get_peripheral_proxy(),
            self.get_devices(),
            self.get_adv_id(),
        );
    }

    pub fn start_adv(
        &self, adv_data: Option<AdvertisingData>, interval: Option<u32>,
    ) -> impl Future<Item = Option<String>, Error = Error> {
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

        match &self.peripheral {
            Some(p) => Right(
                p.start_advertising(&mut ad, None, intv, false)
                    .map_err(|e| e.context("failed to initiate advertise.").into())
                    .and_then(|(status, aid)| match status.error {
                        None => {
                            fx_log_info!(tag: "start_adv", "Started advertising id: {:?}", aid);
                            Ok(aid)
                        }
                        Some(e) => {
                            let err = BTError::from(*e);
                            fx_log_err!(tag: "start_adv", "Failed to start adveritising: {:?}", err);
                            Err(err.into())
                        }
                    }),
            ),
            None => {
                fx_log_err!(tag: "start_adv", "No peripheral created.");
                Left(future::err(
                    BTError::new("No peripheral proxy created.").into(),
                ))
            }
        }
    }

    pub fn stop_adv(&self, aid: String) -> impl Future<Item = (), Error = Error> {
        fx_log_info!(tag: "stop_adv", "stop_adv with aid: {:?}", aid);

        match &self.peripheral {
            Some(p) => Right(
                p.stop_advertising(&aid)
                    .map_err(|e| e.context("failed to stop advertise").into())
                    .and_then(|status| match status.error {
                        Some(e) => {
                            let err = BTError::from(*e);
                            fx_log_err!(tag: "stop_adv", "Failed to stop advertising: {:?}", err);
                            Err(err.into())
                        }
                        None => {
                            fx_log_info!(tag: "stop_adv", "No error in stop adv fut");
                            Ok(())
                        }
                    }),
            ),
            None => {
                fx_log_err!(tag: "stop_adv", "No peripheral proxy created!");
                Left(future::err(
                    BTError::new("No peripheral proxy created.").into(),
                ))
            }
        }
    }

    pub fn start_scan(
        &self, mut filter: Option<ScanFilter>,
    ) -> impl Future<Item = (), Error = Error> {
        match &self.central {
            Some(c) => Right(
                c.start_scan(filter.as_mut().map(OutOfLine))
                    .map_err(|e| e.context("failed to initiate scan.").into())
                    .and_then(|status| match status.error {
                        None => Ok(()),
                        Some(e) => Err(BTError::from(*e).into()),
                    }),
            ),
            None => Left(future::err(
                BTError::new("No central proxy created.").into(),
            )),
        }
    }
}

pub fn listen_central_events(
    bt_facade: Arc<RwLock<BluetoothFacade>>,
) -> impl Future<Item = (), Error = Never> {
    let evt_stream = match bt_facade.read().get_central_proxy() {
        Some(c) => c.take_event_stream(),
        None => panic!("No central created!"),
    };

    evt_stream
        .for_each(move |evt| {
            match evt {
                CentralEvent::OnScanStateChanged { scanning } => {
                    fx_log_info!(tag: "listen_central_events", "Scan state changed: {:?}", scanning);
                }
                CentralEvent::OnDeviceDiscovered { device } => {
                    let id = device.identifier.clone();
                    let name = match &device.advertising_data {
                        Some(adv) => adv.name.clone(),
                        None => None,
                    };

                    fx_log_info!(tag: "listen_central_events", "Device discovered: id: {:?}, name: {:?}", id, name);
                    bt_facade.write().update_devices(id, device);
                }
                CentralEvent::OnPeripheralDisconnected { identifier } => {
                    fx_log_info!(tag: "listen_central_events", "Peer disconnected: {:?}", identifier);
                }
            }
            fok(())
        })
        .map(|_| ())
        .recover(
            |e| fx_log_err!(tag: "listen_central_events", "failed to subscribe to BLE Central events: {:?}", e),
        )
}
