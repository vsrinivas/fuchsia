// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use app;
use async;
use fidl;
use async::temp::Either::{Left, Right};
use bt::error::Error as BTError;
use failure::{Error, Fail, ResultExt};
use std::marker::Unpin;
use fidl::encoding2::OutOfLine;
use fidl_ble::{AdvertisingData, PeripheralMarker, PeripheralProxy, RemoteDevice};
use fidl_ble::{CentralEvent, CentralMarker, CentralProxy, ScanFilter};
use fidl_gatt::{ClientProxy, ServiceInfo};
use futures::future::ready as fready;
use futures::prelude::*;
use parking_lot::RwLock;
use slab::Slab;
use std::collections::HashMap;
use std::mem::PinMut;
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

    // devices: HashMap of key = device id and val = RemoteDevice structs discovered
    // from a scan
    devices: HashMap<String, RemoteDevice>,

    // adv_id: Advertisement ID of device, only one advertisement at a time.
    // TODO(NET-1026): Potentially scale up to a list/set of aid's for concurrent
    // advertisement tests.
    adv_id: Option<String>,

    // peripheral_ids: The identifier for the peripheral of a ConnectPeripheral FIDL call
    // Key = peripheral id, value = ClientProxy
    peripheral_ids: HashMap<String, ClientProxy>,

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
            peripheral_ids: HashMap::new(),
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
    // If no proxy exists, set up central server to listen for events. This central
    // listener will wake up any wakers who may be interested in RemoteDevices
    // discovered
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

    // Set the advertisement ID if none exists already
    pub fn set_adv_id(bt_facade: Arc<RwLock<BluetoothFacade>>, aid: Option<String>) {
        if bt_facade.read().adv_id.is_none() {
            bt_facade.write().adv_id = aid
        } else {
            fx_log_warn!(tag: "set_adv_id", "Current aid: {:?}. Attempted aid: {:?}",
                bt_facade.read().adv_id, aid);
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

    // Given a device id, insert it into the map. If it exists, don't overwrite
    // TODO(aniramakri): Is this right behavior? If the device id already exists, don't
    // overwrite ClientProxy?
    pub fn update_peripheral_id(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: String, client: ClientProxy,
    ) {
        if bt_facade.read().peripheral_ids.contains_key(&id) {
            fx_log_warn!(tag: "update_peripheral_id", "Attempted to overwrite existing id: {}", id);
        } else {
            fx_log_info!(tag: "update_peripheral_id", "Added {:?} to periph ids", id);
            bt_facade.write().peripheral_ids.insert(id.clone(), client);
        }

        fx_log_info!(tag: "update_peripheral_id", "Peripheral ids: {:?}", 
            bt_facade.read().peripheral_ids);
    }

    pub fn remove_peripheral_id(bt_facade: Arc<RwLock<BluetoothFacade>>, id: String) {
        bt_facade.write().peripheral_ids.remove(&id);
        fx_log_info!(tag: "remove_peripheral_id", "After removing peripheral id: {:?}", 
            bt_facade.read().peripheral_ids);
    }

    // Given the devices accrued from scan, returns list of (id, name) devices
    // TODO(NET-1026): Return list of RemoteDevices (unsupported right now
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

    pub fn get_periph_ids(&self) -> HashMap<String, ClientProxy> {
        self.peripheral_ids.clone()
    }

    // Given a device id, return its ClientProxy, if existing, otherwise None
    pub fn get_client_from_peripherals(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: String,
    ) -> Option<ClientProxy> {
        match bt_facade.read().peripheral_ids.get(&id) {
            Some(ref mut c) => Some(c.clone()),
            None => None,
        }
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

    pub fn cleanup_peripheral_ids(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        bt_facade.write().peripheral_ids.clear()
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
        BluetoothFacade::cleanup_peripheral_ids(bt_facade.clone());
    }

    pub fn print(&self) {
        fx_log_info!(tag: "print",
            "BluetoothFacade: Central: {:?}, Periph: {:?}, Devices: {:?}, Adv_id: {:?}, Periph_ids: {:?}",
            self.get_central_proxy(),
            self.get_peripheral_proxy(),
            self.get_devices(),
            self.get_adv_id(),
            self.get_periph_ids(),
        );
    }

    pub fn start_adv(
        bt_facade: Arc<RwLock<BluetoothFacade>>, adv_data: Option<AdvertisingData>,
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
        let facade = bt_facade.clone();
        BluetoothFacade::set_peripheral_proxy(bt_facade.clone());

        match &bt_facade.read().peripheral {
            Some(p) => Right(
                p.start_advertising(&mut ad, None, intv, false)
                    .map_err(|e| e.context("failed to initiate advertise.").into())
                    .and_then(|(status, aid)| match status.error {
                        None => {
                            fx_log_info!(tag: "start_adv", "Started advertising id: {:?}", aid);
                            BluetoothFacade::set_adv_id(facade, aid.clone());
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
                    BTError::new("No peripheral proxy created.").into(),
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
                    BTError::new("No peripheral proxy created.").into(),
                )))
            }
        }
    }

    pub fn start_scan(
        bt_facade: Arc<RwLock<BluetoothFacade>>, mut filter: Option<ScanFilter>,
    ) -> impl Future<Output = Result<(), Error>> {
        BluetoothFacade::cleanup_devices(bt_facade.clone());
        // Set the central proxy if necessary and start a central_listener
        BluetoothFacade::set_central_proxy(bt_facade.clone());

        match &bt_facade.read().central {
            Some(c) => Right(
                c.start_scan(filter.as_mut().map(OutOfLine))
                    .map_err(|e| e.context("failed to initiate scan.").into())
                    .and_then(|status| match status.error {
                        None => fready(Ok(())),
                        Some(e) => fready(Err(BTError::from(*e).into())),
                    }),
            ),
            None => Left(fready(Err(
                BTError::new("No central proxy created.").into(),
            ))),
        }
    }

    pub fn connect_peripheral(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: String,
    ) -> impl Future<Output = Result<(), Error>> {
        // Set the central proxy if necessary
        BluetoothFacade::set_central_proxy(bt_facade.clone());

        let facade = bt_facade.clone();

        // TODO(NET-1026): Move to private method?
        // Create server endpoints
        let (client_end, server_end) = match fidl::endpoints2::create_endpoints() {
            Err(e) => {
                return Left(fready(Err(e.into())));
            }
            Ok(x) => x,
        };

        let mut identifier = id.clone();

        match &bt_facade.read().central {
            Some(c) => Right(
                c.connect_peripheral(&mut identifier, server_end)
                    .map_err(|e| e.context("Failed to connect to peripheral").into())
                    .and_then(move |status| match status.error {
                        None => {
                            // Update the state with the newly connected peripheral id
                            // and client proxy
                            BluetoothFacade::update_peripheral_id(
                                facade,
                                identifier.clone(),
                                client_end,
                            );
                            fready(Ok(()))
                        }
                        Some(e) => fready(Err(BTError::from(*e).into())),
                    }),
            ),
            _ => Left(fready(Err(
                BTError::new("No central proxy created.").into(),
            ))),
        }
    }

    pub fn list_services(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: String,
    ) -> impl Future<Output = Result<Vec<ServiceInfo>, Error>> {
        let client_proxy = BluetoothFacade::get_client_from_peripherals(bt_facade.clone(), id);

        match client_proxy {
            Some(c) => Right(
                c.list_services(None)
                    .map_err(|e| e.context("Failed to list services").into())
                    .and_then(move |(status, services)| match status.error {
                        None => {
                            fx_log_info!(tag: "list_services", "Found services: {:?}", services);
                            fready(Ok(services))
                        }
                        Some(e) => fready(Err(BTError::from(*e).into())),
                    }),
            ),
            None => Left(fready(Err(
                BTError::new("No client exists with provided device id").into(),
            ))),
        }
    }

    pub fn disconnect_peripheral(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: String,
    ) -> impl Future<Output = Result<(), Error>> {
        let facade = bt_facade.clone();

        match &bt_facade.read().central {
            Some(c) => Right(
                c.disconnect_peripheral(&id)
                    .map_err(|e| e.context("Failed to disconnect to peripheral").into())
                    .and_then(move |status| match status.error {
                        None => {
                            // Remove current id from map of peripheral_ids
                            BluetoothFacade::remove_peripheral_id(facade.clone(), id.clone());
                            fready(Ok(()))
                        }
                        Some(e) => fready(Err(BTError::from(*e).into())),
                    }),
            ),
            None => Left(fready(Err(
                BTError::new("No central proxy created.").into(),
            ))),
        }
    }

    // Listens for central events
    pub fn listen_central_events(
        bt_facade: Arc<RwLock<BluetoothFacade>>,
    ) -> impl Future<Output = ()> {
        let evt_stream = match bt_facade.read().central.clone() {
            Some(c) => c.take_event_stream(),
            None => panic!("No central created!"),
        };

        evt_stream
            .map_ok(move |evt| {
                match evt {
                    CentralEvent::OnScanStateChanged { scanning } => {
                        fx_log_info!(tag: "listen_central_events", "Scan state changed: {:?}",
                            scanning);
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

                        // In the event that we need to short-circuit the stream, wake up
                        // all wakers in the host_requests Slab
                        for waker in &bt_facade.read().host_requests {
                            waker.1.wake();
                        }
                    }
                    CentralEvent::OnPeripheralDisconnected { identifier } => {
                        fx_log_info!(tag: "listen_central_events", "Peer disconnected: {:?}", identifier);
                    }
                }
            })
            .try_collect::<()>()
            .unwrap_or_else(|e|
                fx_log_err!(tag: "listen_central_events", "failed to subscribe to BLE Central events: {:?}", e))
    }

    // Is there a way to add this to the BluetoothFacade impl? It requires the bt_facade
    // Arc<RwLock<>> Object, but if it's within the impl, the lock would be unlocked since
    // reference to self.
    pub fn new_devices_found_future(
        bt_facade: Arc<RwLock<BluetoothFacade>>, count: u64,
    ) -> impl Future<Output = ()> {
        OnDeviceFoundFuture::new(bt_facade.clone(), count as usize)
    }
}

/// Custom future that resolves when the number of RemoteDevices discovered equals a
/// device_count param
pub struct OnDeviceFoundFuture {
    bt_facade: Arc<RwLock<BluetoothFacade>>,
    waker_key: Option<usize>,
    device_count: usize,
}

impl Unpin for OnDeviceFoundFuture {}

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
    type Output = ();

    fn poll(mut self: PinMut<Self>, ctx: &mut task::Context) -> Poll<Self::Output> {
        // If the number of devices scanned is less than count, continue scanning
        if self.bt_facade.read().devices.len() < self.device_count {
            let bt_facade = self.bt_facade.clone();
            if self.waker_key.is_none() {
                self.waker_key = Some(bt_facade.write().host_requests.insert(ctx.waker().clone()));
            }
            Poll::Pending
        } else {
            self.remove_waker();
            Poll::Ready(())
        }
    }
}
