// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, Error, ResultExt};
use fidl;
use fidl::encoding::OutOfLine;
use fidl::endpoints;
use fidl_fuchsia_bluetooth_gatt::{Characteristic, RemoteServiceProxy};
use fidl_fuchsia_bluetooth_gatt::{ClientProxy, ServiceInfo};
use fidl_fuchsia_bluetooth_le::RemoteDevice;
use fidl_fuchsia_bluetooth_le::{CentralEvent, CentralMarker, CentralProxy, ScanFilter};
use fuchsia_async as fasync;
use fuchsia_bluetooth::error::Error as BTError;
use fuchsia_component as app;
use fuchsia_syslog::macros::*;
use futures::future::{Future, TryFutureExt};
use futures::stream::TryStreamExt;
use futures::task::Waker;
use parking_lot::RwLock;
use slab::Slab;
use std::collections::HashMap;
use std::sync::Arc;

use crate::bluetooth::types::BleScanResponse;

#[derive(Debug)]
pub struct InnerGattClientFacade {
    // FIDL proxy to the currently connected service, if any.
    active_proxy: Option<RemoteServiceProxy>,

    // central: CentralProxy used for Bluetooth connections
    central: Option<CentralProxy>,

    // devices: HashMap of key = device id and val = RemoteDevice structs discovered from a scan
    devices: HashMap<String, RemoteDevice>,

    // Pending requests to obtain a host
    host_requests: Slab<Waker>,

    // peripheral_ids: The identifier for the peripheral of a ConnectPeripheral FIDL call
    // Key = peripheral id, value = ClientProxy
    peripheral_ids: HashMap<String, ClientProxy>,
}

/// Perform Gatt Client operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct GattClientFacade {
    inner: Arc<RwLock<InnerGattClientFacade>>,
}

impl GattClientFacade {
    pub fn new() -> GattClientFacade {
        GattClientFacade {
            inner: Arc::new(RwLock::new(InnerGattClientFacade {
                central: None,
                devices: HashMap::new(),
                host_requests: Slab::new(),
                peripheral_ids: HashMap::new(),
                active_proxy: None,
            })),
        }
    }

    pub async fn start_scan(&self, mut filter: Option<ScanFilter>) -> Result<(), Error> {
        self.cleanup_devices();
        // Set the central proxy if necessary and start a central_listener
        GattClientFacade::set_central_proxy(self.inner.clone());

        match &self.inner.read().central {
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

    pub async fn gattc_connect_to_service(
        &self,
        periph_id: String,
        service_id: u64,
    ) -> Result<(), Error> {
        let client_proxy = self.get_client_from_peripherals(periph_id);
        let (proxy, server) = endpoints::create_proxy()?;

        // First close the connection to the currently active service.
        if self.inner.read().active_proxy.is_some() {
            self.inner.write().active_proxy = None;
        }
        match client_proxy {
            Some(c) => {
                c.connect_to_service(service_id, server)?;
                self.inner.write().active_proxy = Some(proxy);
                Ok(())
            }
            None => {
                fx_log_err!(tag: "gattc_connect_to_service", "Unable to connect to service.");
                bail!("No peripheral proxy created.")
            }
        }
    }

    pub async fn gattc_discover_characteristics(&self) -> Result<Vec<Characteristic>, Error> {
        let discover_characteristics =
            self.inner.read().active_proxy.as_ref().unwrap().discover_characteristics();

        let (status, chrcs) =
            await!(discover_characteristics).map_err(|_| BTError::new("Failed to send message"))?;
        if let Some(e) = status.error {
            bail!("Failed to read characteristics: {}", BTError::from(*e));
        }
        Ok(chrcs)
    }

    pub async fn gattc_write_char_by_id(
        &self,
        id: u64,
        offset: u16,
        write_value: Vec<u8>,
    ) -> Result<(), Error> {
        let write_characteristic =
            self.inner.read().active_proxy.as_ref().unwrap().write_characteristic(
                id,
                offset,
                &mut write_value.into_iter(),
            );

        let status =
            await!(write_characteristic).map_err(|_| BTError::new("Failed to send message"))?;

        match status.error {
            Some(e) => bail!("Failed to write to characteristic: {}", BTError::from(*e)),
            None => Ok(()),
        }
    }

    pub async fn gattc_write_char_by_id_without_response(
        &self,
        id: u64,
        write_value: Vec<u8>,
    ) -> Result<(), Error> {
        self.inner
            .read()
            .active_proxy
            .as_ref()
            .unwrap()
            .write_characteristic_without_response(id, &mut write_value.into_iter())
            .map_err(|_| BTError::new("Failed to send message").into())
    }

    pub async fn gattc_read_char_by_id(&self, id: u64) -> Result<Vec<u8>, Error> {
        let read_characteristic =
            self.inner.read().active_proxy.as_ref().unwrap().read_characteristic(id);

        let (status, value) =
            await!(read_characteristic).map_err(|_| BTError::new("Failed to send message"))?;

        match status.error {
            Some(e) => bail!("Failed to read characteristic: {}", BTError::from(*e)),
            None => Ok(value),
        }
    }

    pub async fn gattc_read_long_char_by_id(
        &self,
        id: u64,
        offset: u16,
        max_bytes: u16,
    ) -> Result<Vec<u8>, Error> {
        let read_long_characteristic = self
            .inner
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

    pub async fn gattc_read_desc_by_id(&self, id: u64) -> Result<Vec<u8>, Error> {
        let read_descriptor = self.inner.read().active_proxy.as_ref().unwrap().read_descriptor(id);

        let (status, value) =
            await!(read_descriptor).map_err(|_| BTError::new("Failed to send message"))?;

        match status.error {
            Some(e) => bail!("Failed to read descriptor: {}", BTError::from(*e)),
            None => Ok(value),
        }
    }

    pub async fn gattc_read_long_desc_by_id(
        &self,
        id: u64,
        offset: u16,
        max_bytes: u16,
    ) -> Result<Vec<u8>, Error> {
        let read_long_descriptor = self
            .inner
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

    pub async fn gattc_write_desc_by_id(&self, id: u64, write_value: Vec<u8>) -> Result<(), Error> {
        let write_descriptor = self
            .inner
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
        &self,
        id: u64,
        value: bool,
    ) -> Result<(), Error> {
        let notify_characteristic =
            self.inner.read().active_proxy.as_ref().unwrap().notify_characteristic(id, value);

        let status =
            await!(notify_characteristic).map_err(|_| BTError::new("Failed to send message"))?;

        match status.error {
            Some(e) => bail!("Failed to enable notifications: {}", BTError::from(*e)),
            None => {}
        };
        Ok(())
    }

    pub async fn list_services(&self, id: String) -> Result<Vec<ServiceInfo>, Error> {
        let client_proxy = self.get_client_from_peripherals(id);

        match client_proxy {
            Some(c) => {
                let (status, services) = await!(c.list_services(None))?;
                match status.error {
                    None => {
                        fx_log_info!(tag: "list_services", "Found services: {:?}", services);
                        Ok(services)
                    }
                    Some(e) => bail!("Error found while listing services: {:?}", BTError::from(*e)),
                }
            }
            None => bail!("No client exists with provided device id"),
        }
    }

    // Given a device id, return its ClientProxy, if existing, otherwise None
    pub fn get_client_from_peripherals(&self, id: String) -> Option<ClientProxy> {
        self.inner.read().peripheral_ids.get(&id).map(|c| c.clone())
    }

    // Given a device id, insert it into the map. If it exists, don't overwrite
    // TODO(aniramakri): Is this right behavior? If the device id already exists, don't
    // overwrite ClientProxy?
    pub fn update_peripheral_id(&self, id: &String, client: ClientProxy) {
        if self.inner.read().peripheral_ids.contains_key(id) {
            fx_log_warn!(tag: "update_peripheral_id", "Attempted to overwrite existing id: {}", id);
        } else {
            fx_log_info!(tag: "update_peripheral_id", "Added {:?} to peripheral ids", id);
            self.inner.write().peripheral_ids.insert(id.clone(), client);
        }

        fx_log_info!(tag: "update_peripheral_id", "Peripheral ids: {:?}",
            self.inner.read().peripheral_ids);
    }

    pub fn remove_peripheral_id(&self, id: &String) {
        self.inner.write().peripheral_ids.remove(id);
        fx_log_info!(tag: "remove_peripheral_id", "After removing peripheral id: {:?}",
            self.inner.read().peripheral_ids);
    }

    // Update the central proxy if none exists, otherwise raise error
    // If no proxy exists, set up central server to listen for events. This central listener will
    // wake up any wakers who may be interested in RemoteDevices discovered
    pub fn set_central_proxy(inner: Arc<RwLock<InnerGattClientFacade>>) {
        let mut central_modified = false;
        let new_central = match inner.read().central.clone() {
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
        inner.write().central = new_central;
        // Only spawn if a central hadn't been created
        if !central_modified {
            fasync::spawn(GattClientFacade::listen_central_events(inner.clone()))
        }
    }

    // Update the devices dictionary with a discovered RemoteDevice
    pub fn update_devices(
        inner: &Arc<RwLock<InnerGattClientFacade>>,
        id: String,
        device: RemoteDevice,
    ) {
        if inner.read().devices.contains_key(&id) {
            fx_log_warn!(tag: "update_devices", "Already discovered: {:?}", id);
        } else {
            inner.write().devices.insert(id, device);
        }
    }

    pub fn listen_central_events(
        inner: Arc<RwLock<InnerGattClientFacade>>,
    ) -> impl Future<Output = ()> {
        let evt_stream = match inner.read().central.clone() {
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
                        let name = device.advertising_data.as_ref().map(|adv| &adv.name);
                        // Update the device discovered list
                        fx_log_info!(tag: "listen_central_events", "Device discovered: id: {:?}, name: {:?}", id, name);
                        GattClientFacade::update_devices(&inner, id, device);

                        // In the event that we need to short-circuit the stream, wake up all
                        // wakers in the host_requests Slab
                        for waker in &inner.read().host_requests {
                            waker.1.wake_by_ref();
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

    pub async fn connect_peripheral(&self, id: String) -> Result<(), Error> {
        // Set the central proxy if necessary
        GattClientFacade::set_central_proxy(self.inner.clone());

        // TODO(NET-1026): Move to private method?
        // Create server endpoints
        let (proxy, server_end) = match fidl::endpoints::create_proxy() {
            Err(e) => {
                bail!("Failed to create proxy endpoint: {:?}", e);
            }
            Ok(x) => x,
        };

        let mut identifier = id.clone();
        match &self.inner.read().central {
            Some(c) => {
                let status = await!(c.connect_peripheral(&mut identifier, server_end))?;
                match status.error {
                    Some(e) => bail!("Failed to connect to peripheral: {}", BTError::from(*e)),
                    None => {}
                }
            }
            None => bail!("No central proxy created."),
        };
        self.update_peripheral_id(&identifier, proxy);
        Ok(())
    }

    pub async fn disconnect_peripheral(&self, id: String) -> Result<(), Error> {
        match &self.inner.read().central {
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
        self.remove_peripheral_id(&id);
        Ok(())
    }

    // Return the central proxy
    pub fn get_central_proxy(&self) -> Option<CentralProxy> {
        self.inner.read().central.clone()
    }

    pub fn get_periph_ids(&self) -> HashMap<String, ClientProxy> {
        self.inner.read().peripheral_ids.clone()
    }

    // Given the devices accrued from scan, returns list of (id, name) devices
    // TODO(NET-1291): Return list of RemoteDevices (unsupported right now
    // because Clone() not implemented for RemoteDevice)
    pub fn get_devices(&self) -> Vec<BleScanResponse> {
        const EMPTY_DEVICE: &str = "";
        let mut devices = Vec::new();
        for val in self.inner.read().devices.keys() {
            let name = match &self.inner.read().devices[val].advertising_data {
                Some(adv) => adv.name.clone().unwrap_or(EMPTY_DEVICE.to_string()),
                None => EMPTY_DEVICE.to_string(),
            };
            let connectable = self.inner.read().devices[val].connectable;
            devices.push(BleScanResponse::new(val.clone(), name, connectable));
        }

        devices
    }

    pub fn cleanup_central_proxy(&self) {
        self.inner.write().central = None
    }

    pub fn cleanup_devices(&self) {
        self.inner.write().devices.clear()
    }

    pub fn cleanup_central(&self) {
        self.cleanup_central_proxy();
        self.cleanup_devices();
    }

    pub fn cleanup_peripheral_ids(&self) {
        self.inner.write().peripheral_ids.clear()
    }

    pub fn print(&self) {
        fx_log_info!(tag: "print",
            "BluetoothFacade: Central: {:?}, Devices: {:?}, Periph_ids: {:?}",
            self.get_central_proxy(),
            self.get_devices(),
            self.get_periph_ids(),
        );
    }

    // Close both central proxies
    pub fn cleanup(&self) {
        self.cleanup_central();
        self.cleanup_peripheral_ids();
    }
}
