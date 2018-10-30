// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, Error, Fail, ResultExt};
use fidl;
use fidl::encoding::OutOfLine;
use fidl::endpoints;
use fidl_fuchsia_bluetooth_gatt::{
    Characteristic, LocalServiceDelegateMarker, LocalServiceMarker, LocalServiceProxy,
    RemoteServiceProxy, Server_Marker, Server_Proxy,
};
use fidl_fuchsia_bluetooth_gatt::{ClientProxy, ServiceInfo};
use fidl_fuchsia_bluetooth_le::RemoteDevice;
use fidl_fuchsia_bluetooth_le::{CentralEvent, CentralMarker, CentralProxy, ScanFilter};
use fuchsia_app as app;
use fuchsia_async::{
    self as fasync,
    temp::Either::{Left, Right},
};
use fuchsia_bluetooth::error::Error as BTError;
use fuchsia_syslog::macros::*;
use fuchsia_zircon as zx;
use futures::future::{ready as fready, Future, TryFutureExt};
use futures::stream::TryStreamExt;
use futures::task::Waker;
use parking_lot::RwLock;
use slab::Slab;
use std::collections::HashMap;
use std::sync::Arc;

// Sl4f-Constants and Bluetooth related functionality
//use crate::bluetooth::constants::*;
use crate::bluetooth::types::BleScanResponse;

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

    // devices: HashMap of key = device id and val = RemoteDevice structs discovered from a scan
    devices: HashMap<String, RemoteDevice>,

    // peripheral_ids: The identifier for the peripheral of a ConnectPeripheral FIDL call
    // Key = peripheral id, value = ClientProxy
    peripheral_ids: HashMap<String, ClientProxy>,

    // Pending requests to obtain a host
    host_requests: Slab<Waker>,

    // GATT related state
    // server_proxy: The proxy for Gatt server
    server_proxy: Option<Server_Proxy>,

    // service_proxies: HashMap of key = String (randomly generated local_service_id) and val:
    // LocalServiceProxy
    service_proxies: HashMap<String, (LocalServiceProxy, fasync::Channel)>,

    // FIDL proxy to the currently connected service, if any.
    active_proxy: Option<RemoteServiceProxy>,
}

impl BluetoothFacade {
    pub fn new(central_proxy: Option<CentralProxy>) -> Arc<RwLock<BluetoothFacade>> {
        Arc::new(RwLock::new(BluetoothFacade {
            central: central_proxy,
            devices: HashMap::new(),
            peripheral_ids: HashMap::new(),
            host_requests: Slab::new(),
            server_proxy: None,
            service_proxies: HashMap::new(),
            active_proxy: None,
        }))
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
            fasync::spawn(BluetoothFacade::listen_central_events(bt_facade.clone()))
        }
    }

    pub fn set_server_proxy(bt_facade: &RwLock<BluetoothFacade>) {
        let new_server = match bt_facade.read().server_proxy.clone() {
            Some(s) => {
                fx_log_info!(tag: "set_server_proxy", "Current service proxy: {:?}", s);
                Some(s)
            }
            None => {
                fx_log_info!(tag: "set_server_proxy", "Setting new server proxy");
                Some(
                    app::client::connect_to_service::<Server_Marker>()
                        .context("Failed to connect to service.")
                        .unwrap(),
                )
            }
        };

        bt_facade.write().server_proxy = new_server;
    }

    // Update the devices dictionary with a discovered RemoteDevice
    pub fn update_devices(bt_facade: &RwLock<BluetoothFacade>, id: String, device: RemoteDevice) {
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
        bt_facade: &RwLock<BluetoothFacade>, id: &String, client: ClientProxy,
    ) {
        if bt_facade.read().peripheral_ids.contains_key(id) {
            fx_log_warn!(tag: "update_peripheral_id", "Attempted to overwrite existing id: {}", id);
        } else {
            fx_log_info!(tag: "update_peripheral_id", "Added {:?} to peripheral ids", id);
            bt_facade.write().peripheral_ids.insert(id.clone(), client);
        }

        fx_log_info!(tag: "update_peripheral_id", "Peripheral ids: {:?}",
            bt_facade.read().peripheral_ids);
    }

    pub fn remove_peripheral_id(bt_facade: &RwLock<BluetoothFacade>, id: &String) {
        bt_facade.write().peripheral_ids.remove(id);
        fx_log_info!(tag: "remove_peripheral_id", "After removing peripheral id: {:?}",
            bt_facade.read().peripheral_ids);
    }

    // Given the devices accrued from scan, returns list of (id, name) devices
    // TODO(NET-1291): Return list of RemoteDevices (unsupported right now
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

    pub fn get_periph_ids(&self) -> HashMap<String, ClientProxy> {
        self.peripheral_ids.clone()
    }

    // Given a device id, return its ClientProxy, if existing, otherwise None
    pub fn get_client_from_peripherals(
        bt_facade: &RwLock<BluetoothFacade>, id: String,
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

    pub fn get_server_proxy(&self) -> &Option<Server_Proxy> {
        &self.server_proxy
    }

    pub fn get_service_proxies(&self) -> &HashMap<String, (LocalServiceProxy, fasync::Channel)> {
        &self.service_proxies
    }

    pub fn cleanup_server_proxy(bt_facade: &RwLock<BluetoothFacade>) {
        bt_facade.write().server_proxy = None
    }

    pub fn cleanup_service_proxies(bt_facade: &RwLock<BluetoothFacade>) {
        bt_facade.write().service_proxies.clear()
    }

    pub fn cleanup_devices(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        bt_facade.write().devices.clear()
    }

    pub fn cleanup_peripheral_ids(bt_facade: &RwLock<BluetoothFacade>) {
        bt_facade.write().peripheral_ids.clear()
    }

    pub fn cleanup_central_proxy(bt_facade: &RwLock<BluetoothFacade>) {
        bt_facade.write().central = None
    }

    pub fn cleanup_central(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        BluetoothFacade::cleanup_central_proxy(&bt_facade);
        BluetoothFacade::cleanup_devices(bt_facade.clone());
    }

    pub fn cleanup_gatt(bt_facade: &RwLock<BluetoothFacade>) {
        BluetoothFacade::cleanup_server_proxy(bt_facade.clone());
        BluetoothFacade::cleanup_service_proxies(bt_facade.clone());
    }

    // Close both central and peripheral proxies
    pub fn cleanup(bt_facade: Arc<RwLock<BluetoothFacade>>) {
        BluetoothFacade::cleanup_central(bt_facade.clone());
        BluetoothFacade::cleanup_peripheral_ids(&bt_facade);
        BluetoothFacade::cleanup_gatt(&bt_facade);
    }

    pub fn print(&self) {
        fx_log_info!(tag: "print",
            "BluetoothFacade: Central: {:?}, Devices: {:?}, Periph_ids: {:?}, Server Proxy: {:?}, Services: {:?}",
            self.get_central_proxy(),
            self.get_devices(),
            self.get_periph_ids(),
            self.get_server_proxy(),
            self.get_service_proxies(),
        );
    }

    pub async fn start_scan(
        bt_facade: Arc<RwLock<BluetoothFacade>>, mut filter: Option<ScanFilter>,
    ) -> Result<(), Error> {
        BluetoothFacade::cleanup_devices(bt_facade.clone());
        // Set the central proxy if necessary and start a central_listener
        BluetoothFacade::set_central_proxy(bt_facade.clone());

        match &bt_facade.read().central {
            Some(c) => {
                let status = await!(c.start_scan(filter.as_mut().map(OutOfLine)))?;
                match status.error {
                    Some(e) => bail!("Failed to start scan: {}", BTError::from(*e)),
                    None => Ok(()),
                }
            }
            None => bail!("No central proxy created."),
        }
    }

    pub async fn connect_peripheral(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: String,
    ) -> Result<(), Error> {
        // Set the central proxy if necessary
        BluetoothFacade::set_central_proxy(bt_facade.clone());

        // TODO(NET-1026): Move to private method?
        // Create server endpoints
        let (proxy, server_end) = match fidl::endpoints::create_proxy() {
            Err(e) => {
                bail!("Failed to create proxy endpoint: {:?}", e);
            }
            Ok(x) => x,
        };

        let mut identifier = id.clone();
        match &bt_facade.read().central {
            Some(c) => {
                let status = await!(c.connect_peripheral(&mut identifier, server_end))?;
                match status.error {
                    Some(e) => bail!("Failed to connect to peripheral: {}", BTError::from(*e)),
                    None => {}
                }
            }
            None => bail!("No central proxy created."),
        };
        BluetoothFacade::update_peripheral_id(&bt_facade, &identifier, proxy);
        Ok(())
    }

    pub fn list_services(
        bt_facade: &RwLock<BluetoothFacade>, id: String,
    ) -> impl Future<Output = Result<Vec<ServiceInfo>, Error>> {
        let client_proxy = BluetoothFacade::get_client_from_peripherals(&bt_facade, id);

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
            None => Left(fready(Err(BTError::new(
                "No client exists with provided device id",
            )
            .into()))),
        }
    }

    pub async fn gattc_connect_to_service(
        bt_facade: &RwLock<BluetoothFacade>, periph_id: String, service_id: u64,
    ) -> Result<(), Error> {
        let client_proxy = BluetoothFacade::get_client_from_peripherals(&bt_facade, periph_id);
        let (proxy, server) = endpoints::create_proxy()?;

        // First close the connection to the currently active service.
        if bt_facade.read().active_proxy.is_some() {
            bt_facade.write().active_proxy = None;
        }
        match client_proxy {
            Some(c) => {
                c.connect_to_service(service_id, server)?;
                bt_facade.write().active_proxy = Some(proxy);
                Ok(())
            }
            None => {
                fx_log_err!(tag: "gattc_connect_to_service", "Unable to connect to service.");
                bail!("No peripheral proxy created.")
            }
        }
    }

    pub async fn gattc_discover_characteristics(
        bt_facade: Arc<RwLock<BluetoothFacade>>,
    ) -> Result<Vec<Characteristic>, Error> {
        let discover_characteristics = bt_facade
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .discover_characteristics();

        let (status, chrcs) =
            await!(discover_characteristics).map_err(|_| BTError::new("Failed to send message"))?;
        if let Some(e) = status.error {
            bail!("Failed to read characteristics: {}", BTError::from(*e));
        }
        Ok(chrcs)
    }

    pub async fn gattc_write_char_by_id(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: u64, offset: u16, write_value: Vec<u8>,
    ) -> Result<(), Error> {
        let write_characteristic = bt_facade
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .write_characteristic(id, offset, &mut write_value.into_iter());

        let status =
            await!(write_characteristic).map_err(|_| BTError::new("Failed to send message"))?;

        match status.error {
            Some(e) => bail!("Failed to write to characteristic: {}", BTError::from(*e)),
            None => Ok(()),
        }
    }

    pub async fn gattc_write_char_by_id_without_response(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: u64, write_value: Vec<u8>,
    ) -> Result<(), Error> {
        bt_facade
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .write_characteristic_without_response(id, &mut write_value.into_iter())
            .map_err(|_| BTError::new("Failed to send message").into())
    }

    pub async fn gattc_read_char_by_id(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: u64,
    ) -> Result<Vec<u8>, Error> {
        let read_characteristic = bt_facade
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .read_characteristic(id);

        let (status, value) =
            await!(read_characteristic).map_err(|_| BTError::new("Failed to send message"))?;

        match status.error {
            Some(e) => bail!("Failed to read characteristic: {}", BTError::from(*e)),
            None => Ok(value),
        }
    }

    pub async fn gattc_read_long_char_by_id(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: u64, offset: u16, max_bytes: u16,
    ) -> Result<Vec<u8>, Error> {
        let read_long_characteristic = bt_facade
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .read_long_characteristic(id, offset, max_bytes);

        let (status, value) =
            await!(read_long_characteristic).map_err(|_| BTError::new("Failed to send message"))?;

        match status.error {
            Some(e) => bail!("Failed to read characteristic: {}", BTError::from(*e)),
            None => Ok(value),
        }
    }

    pub async fn gattc_read_desc_by_id(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: u64,
    ) -> Result<Vec<u8>, Error> {
        let read_descriptor = bt_facade
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .read_descriptor(id);

        let (status, value) =
            await!(read_descriptor).map_err(|_| BTError::new("Failed to send message"))?;

        match status.error {
            Some(e) => bail!("Failed to read descriptor: {}", BTError::from(*e)),
            None => Ok(value),
        }
    }

    pub async fn gattc_read_long_desc_by_id(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: u64, offset: u16, max_bytes: u16,
    ) -> Result<Vec<u8>, Error> {
        let read_long_descriptor = bt_facade
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .read_long_descriptor(id, offset, max_bytes);

        let (status, value) =
            await!(read_long_descriptor).map_err(|_| BTError::new("Failed to send message"))?;

        match status.error {
            Some(e) => bail!("Failed to read descriptor: {}", BTError::from(*e)),
            None => Ok(value),
        }
    }

    pub async fn gattc_write_desc_by_id(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: u64, write_value: Vec<u8>,
    ) -> Result<(), Error> {
        let write_descriptor = bt_facade
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .write_descriptor(id, &mut write_value.into_iter());

        let status =
            await!(write_descriptor).map_err(|_| BTError::new("Failed to send message"))?;

        match status.error {
            Some(e) => bail!("Failed to write to descriptor: {}", BTError::from(*e)),
            None => Ok(()),
        }
    }

    pub async fn gattc_toggle_notify_characteristic(
        bt_facade: Arc<RwLock<BluetoothFacade>>, id: u64, value: bool,
    ) -> Result<(), Error> {
        let notify_characteristic = bt_facade
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .notify_characteristic(id, value);

        let status =
            await!(notify_characteristic).map_err(|_| BTError::new("Failed to send message"))?;

        match status.error {
            Some(e) => bail!("Failed to enable notifications: {}", BTError::from(*e)),
            None => {}
        };
        Ok(())
    }

    pub async fn disconnect_peripheral(
        bt_facade: &RwLock<BluetoothFacade>, id: String,
    ) -> Result<(), Error> {
        match &bt_facade.read().central {
            Some(c) => {
                let status = await!(c.disconnect_peripheral(&id))?;
                match status.error {
                    None => {}
                    Some(e) => bail!("Failed to disconnect: {:?}", e),
                }
            }
            None => bail!("Failed to disconnect from perpheral."),
        };
        // Remove current id from map of peripheral_ids
        BluetoothFacade::remove_peripheral_id(&bt_facade, &id);
        Ok(())
    }

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
                        BluetoothFacade::update_devices(&bt_facade, id, device);

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
            })
            .try_collect::<()>()
            .unwrap_or_else(
                |e| fx_log_err!(tag: "listen_central_events", "failed to subscribe to BLE Central events: {:?}", e),
            )
    }

    pub fn publish_service(
        bt_facade: &RwLock<BluetoothFacade>, mut service_info: ServiceInfo,
        local_service_id: String,
    ) -> impl Future<Output = Result<(), Error>> {
        // Set the local peripheral proxy if necessary
        BluetoothFacade::set_server_proxy(&bt_facade);

        // If the unique service_proxy id already exists, reject publishing of service
        if bt_facade
            .read()
            .service_proxies
            .contains_key(&local_service_id)
        {
            fx_log_err!(tag: "publish_service", "Attempted to create service proxy for existing key. {:?}", local_service_id.clone());
            return Left(fready(Err(BTError::new(
                "Proxy key already exists, aborting.",
            )
            .into())));
        }

        // TODO(NET-1289): Ensure unwrap() safety
        let (service_local, service_remote) = zx::Channel::create().unwrap();
        let service_local = fasync::Channel::from_channel(service_local).unwrap();
        let service_server = fidl::endpoints::ServerEnd::<LocalServiceMarker>::new(service_remote);

        // Otherwise, store the local proxy in map with unique local_service_id string
        let service_proxy = LocalServiceProxy::new(service_local);

        let (delegate_local, delegate_remote) = zx::Channel::create().unwrap();
        let delegate_local = fasync::Channel::from_channel(delegate_local).unwrap();
        let delegate_ptr =
            fidl::endpoints::ClientEnd::<LocalServiceDelegateMarker>::new(delegate_remote);

        bt_facade
            .write()
            .service_proxies
            .insert(local_service_id, (service_proxy, delegate_local));

        match &bt_facade.read().server_proxy {
            Some(server) => {
                let pub_fut = server
                    .publish_service(&mut service_info, delegate_ptr, service_server)
                    .map_err(|e| Error::from(e.context("Publishing service error")))
                    .and_then(|status| match status.error {
                        None => fready(Ok(())),
                        Some(e) => fready(Err(BTError::from(*e).into())),
                    });

                Right(pub_fut)
            }
            None => Left(fready(
                Err(BTError::new("No central proxy created.").into()),
            )),
        }
    }
}
