// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use app;
use async;
use bt::error::Error as BTError;
use failure::{Error, Fail, ResultExt};
use fidl::encoding2::OutOfLine;
use fidl_ble::{AdvertisingData, PeripheralMarker, PeripheralProxy, RemoteDevice};
use fidl_ble::{CentralEvent, CentralMarker, CentralProxy, ScanFilter};
use futures::future;
use futures::future::ok as fok;
use futures::future::Either::{Left, Right};
use futures::prelude::*;
use parking_lot::RwLock;
use slab::Slab;
use std::collections::HashMap;
use std::sync::Arc;

// Sl4f-Constants and Bluetooth related functionality
use bluetooth::constants::*;
use bluetooth::types::{BleAdvertiseResponse, BleScanResponse};

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

    // Pending requests to obtain a host
    host_requests: Slab<task::Waker>,
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
            host_requests: Slab::new(),
        }))
    }

    // Set the peripheral proxy only if none exists, otherwise, use existing
    pub fn set_peripheral_proxy(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        let new_periph = match bt_facade.read().peripheral.clone() {
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

        bt_facade.write().peripheral = new_periph
    }

    // Update the central proxy if none exists, otherwise raise error
    // If no proxy exists, set up central server to listen for events. This central listener will
    // wake up any wakers who may be interested in RemoteDevices discovered
    pub fn set_central_proxy(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        let mut central_modified = false;
        let new_central = match bt_facade.read().central.clone() {
            Some(c) => {
                fx_log_warn!(tag: "set_central_proxy", "Current central: {:?}.", c);
                central_modified = true;
                Some(c)
            }
            None => {
                let central_svc: CentralProxy = app::client::connect_to_service::<CentralMarker>()
                    .context("Failed to connect to BLE Central service.")
                    .unwrap();
                Some(central_svc)
            }
        };

        // Update the central with the (potentially) newly created proxy
        bt_facade.write().central = new_central;
        // Only spawn if a central hadn't been created
        if !central_modified {
            async::spawn(BluetoothFacade::listen_central_events(bt_facade.clone()))
        }
    }

    // Update the devices dictionary with a discovered RemoteDevice
    pub fn update_devices(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: String, device: RemoteDevice,
    ) {
        if bt_facade.read().devices.contains_key(&id) {
            fx_log_warn!(tag: "update_devices", "Already discovered: {:?}", id);
        } else {
            bt_facade.write().devices.insert(id, device);
        }
    }

    // Update the advertisement ID if none exists already
    pub fn update_adv_id(bt_facade: Arc<RwLock<BluetoothFacade>>, aid: Option<String>) {
        if bt_facade.read().adv_id.is_none() {
            bt_facade.write().adv_id = aid
        } else {
            fx_log_warn!(tag: "update_adv_id", "Current aid: {:?}. Attempted aid: {:?}", bt_facade.read().adv_id, aid);
        }
    }

    // Given the devices accrued from scan, returns list of (id, name) devices
    // TODO(aniramakri): Return list of RemoteDevices (unsupported right now
    // because Clone() not implemented for RemoteDevice)
    pub fn get_devices(&self) -> Vec<BleScanResponse> {
        const EMPTY_DEVICE: &str = "";
        let mut devices = Vec::new();
        for val in self.devices.keys() {
            let name = match &self.devices[val].advertising_data {
                Some(adv) => adv.name.clone().unwrap_or(EMPTY_DEVICE.to_string()),
                None => EMPTY_DEVICE.to_string(),
            };
            let connectable = self.devices[val].connectable;
            devices.push(BleScanResponse::new(val.clone(), name, connectable));
        }

        devices
    }

    pub fn get_adv_id(&self) -> BleAdvertiseResponse {
        BleAdvertiseResponse::new(self.adv_id.clone())
    }

    // Return the central proxy
    pub fn get_central_proxy(&self) -> &Option<CentralProxy> {
        &self.central
    }

    pub fn get_peripheral_proxy(&self) -> &Option<PeripheralProxy> {
        &self.peripheral
    }

    // Close peripheral proxy
    pub fn cleanup_peripheral_proxy(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        bt_facade.write().peripheral = None;
    }

    pub fn cleanup_central_proxy(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        bt_facade.write().central = None
    }

    pub fn cleanup_devices(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        bt_facade.write().devices.clear()
    }

    pub fn cleanup_adv_id(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        bt_facade.write().adv_id = None
    }

    pub fn cleanup_central(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        BluetoothFacade::cleanup_central_proxy(bt_facade.clone());
        BluetoothFacade::cleanup_devices(bt_facade.clone());
    }

    pub fn cleanup_peripheral(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        BluetoothFacade::cleanup_peripheral_proxy(bt_facade.clone());
        BluetoothFacade::cleanup_adv_id(bt_facade.clone());
    }

    // Close both central and peripheral proxies
    pub fn cleanup(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        BluetoothFacade::cleanup_peripheral(bt_facade.clone());
        BluetoothFacade::cleanup_central(bt_facade.clone());
    }

    pub fn print(&self) {
        fx_log_info!(tag: "print",
            "BluetoothFacade: {:?}, {:?}, {:?}, {:?}",
            self.get_central_proxy(),
            self.get_peripheral_proxy(),
            self.get_devices(),
            self.get_adv_id(),
        );
    }

    pub fn start_adv(
        bt_facade: Arc<RwLock<BluetoothFacade>>, adv_data: Option<AdvertisingData>,
        interval: Option<u32>,
    ) -> impl Future<Item = (), Error = Error> {
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
        let facade = bt_facade.clone();
        BluetoothFacade::set_peripheral_proxy(bt_facade.clone());

        match &bt_facade.read().peripheral {
            Some(p) => Right(
                p.start_advertising(&mut ad, None, intv, false)
                    .map_err(|e| e.context("failed to initiate advertise.").into())
                    .and_then(|(status, aid)| match status.error {
                        None => {
                            fx_log_info!(tag: "start_adv", "Started advertising id: {:?}", aid);
                            BluetoothFacade::update_adv_id(facade, aid.clone());
                            Ok(())
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
                        None => Ok(()),
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
        bt_facade: Arc<RwLock<BluetoothFacade>>, mut filter: Option<ScanFilter>,
    ) -> impl Future<Item = (), Error = Error> {
        BluetoothFacade::cleanup_devices(bt_facade.clone());
        // Set the central proxy if necessary and start a central_listener
        BluetoothFacade::set_central_proxy(bt_facade.clone());

        match &bt_facade.read().central {
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
    // Listens for central events
    pub fn listen_central_events(
        bt_facade: Arc<RwLock<BluetoothFacade>>,
    ) -> impl Future<Item = (), Error = Never> {
        let evt_stream = match bt_facade.read().central.clone() {
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

                        // Update the device discovered list
                        fx_log_info!(tag: "listen_central_events", "Device discovered: id: {:?}, name: {:?}", id, name);
                        BluetoothFacade::update_devices(bt_facade.clone(), id, device);

                        // In the event that we need to short-circuit the stream, wake up all
                        // wakers in the host_requests Slab
                        for waker in &bt_facade.read().host_requests {
                            waker.1.wake();
                        }
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

    // Is there a way to add this to the BluetoothFacade impl? It requires the bt_facade
    // Arc<RwLock<>> Object, but if it's within the impl, the lock would be unlocked since
    // reference to self.
    pub fn new_devices_found_future(
        bt_facade: Arc<RwLock<BluetoothFacade>>, count: u64,
    ) -> impl Future<Item = (), Error = Never> {
        OnDeviceFoundFuture::new(bt_facade.clone(), count as usize)
    }
}

/// Custom future that resolves when the number of RemoteDevices discovered equals a device_count
/// param No builtin timeout, instead, chain this future with an on_timeout() wrapper
pub struct OnDeviceFoundFuture {
    bt_facade: Arc<RwLock<BluetoothFacade>>,
    waker_key: Option<usize>,
    device_count: usize,
}

impl OnDeviceFoundFuture {
    fn new(bt_facade: Arc<RwLock<BluetoothFacade>>, count: usize) -> OnDeviceFoundFuture {
        OnDeviceFoundFuture {
            bt_facade: bt_facade.clone(),
            waker_key: None,
            device_count: count,
        }
    }

    fn remove_waker(&mut self) {
        if let Some(key) = self.waker_key {
            self.bt_facade.write().host_requests.remove(key);
        }

        self.waker_key = None;
    }
}

impl Future for OnDeviceFoundFuture {
    type Item = ();
    type Error = Never;

    fn poll(&mut self, ctx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        // If the number of devices scanned is less than count, continue scanning
        if self.bt_facade.read().devices.len() < self.device_count {
            if self.waker_key.is_none() {
                self.waker_key = Some(
                    self.bt_facade
                        .write()
                        .host_requests
                        .insert(ctx.waker().clone()),
                );
            }
            Ok(Async::Pending)
        } else {
            self.remove_waker();
            Ok(Async::Ready(()))
        }
    }
}
