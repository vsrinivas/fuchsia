// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl;
use fidl::endpoints;
use fidl_fuchsia_bluetooth_gatt::{Characteristic, ReliableMode, RemoteServiceProxy, WriteOptions};
use fidl_fuchsia_bluetooth_gatt::{ClientProxy, ServiceInfo};
use fidl_fuchsia_bluetooth_le::RemoteDevice;
use fidl_fuchsia_bluetooth_le::{
    CentralEvent, CentralMarker, CentralProxy, ConnectionOptions, ScanFilter,
};
use fuchsia_async as fasync;
use fuchsia_component as app;
use fuchsia_syslog::macros::*;
use futures::future::{Future, TryFutureExt};
use futures::stream::TryStreamExt;
use futures::task::Waker;
use parking_lot::RwLock;
use slab::Slab;
use std::collections::HashMap;
use std::str::FromStr;
use std::sync::Arc;

use fidl_fuchsia_bluetooth;
use fuchsia_bluetooth::types::Uuid;

use crate::bluetooth::types::{BleScanResponse, SerializableReadByTypeResult};
use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use crate::common_utils::error::Sl4fError;

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
                let status = c.start_scan(filter.as_mut()).await?;
                match status.error {
                    Some(e) => {
                        return Err(format_err!("Failed to start scan: {}", Sl4fError::from(*e)))
                    }
                    None => Ok(()),
                }
            }
            None => return Err(format_err!("No central proxy created.")),
        }
    }

    pub async fn gattc_connect_to_service(
        &self,
        periph_id: String,
        service_id: u64,
    ) -> Result<(), Error> {
        let tag = "GattClientFacade::gattc_connect_to_service";
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
                fx_log_err!(tag: &with_line!(tag), "Unable to connect to service.");
                return Err(format_err!("No peripheral proxy created."));
            }
        }
    }

    pub async fn gattc_discover_characteristics(&self) -> Result<Vec<Characteristic>, Error> {
        let tag = "GattClientFacade::gattc_discover_characteristics";
        let discover_characteristics = match &self.inner.read().active_proxy {
            Some(proxy) => proxy.discover_characteristics(),
            None => fx_err_and_bail!(&with_line!(tag), "Central proxy not available."),
        };

        let (status, chrcs) =
            discover_characteristics.await.map_err(|_| Sl4fError::new("Failed to send message"))?;
        if let Some(e) = status.error {
            let err_msg = format!("Failed to read characteristics: {}", Sl4fError::from(*e));
            fx_err_and_bail!(&with_line!(tag), err_msg)
        }
        Ok(chrcs)
    }

    pub async fn gattc_write_char_by_id(&self, id: u64, write_value: Vec<u8>) -> Result<(), Error> {
        let tag = "GattClientFacade::gattc_write_char_by_id";

        let write_characteristic = match &self.inner.read().active_proxy {
            Some(proxy) => proxy.write_characteristic(id, &write_value),
            None => fx_err_and_bail!(&with_line!(tag), "Central proxy not available."),
        };

        let status =
            write_characteristic.await.map_err(|_| Sl4fError::new("Failed to send message"))?;

        match status.error {
            Some(e) => {
                let err_msg = format!("Failed to write to characteristic: {}", Sl4fError::from(*e));
                fx_err_and_bail!(&with_line!(tag), err_msg)
            }
            None => Ok(()),
        }
    }

    pub async fn gattc_write_long_char_by_id(
        &self,
        id: u64,
        offset: u16,
        write_value: Vec<u8>,
        reliable_mode: bool,
    ) -> Result<(), Error> {
        let tag = "GattClientFacade::gattc_write_long_char_by_id";

        let reliable_mode = match reliable_mode {
            true => ReliableMode::Enabled,
            false => ReliableMode::Disabled,
        };

        let write_long_characteristic = match &self.inner.read().active_proxy {
            Some(proxy) => proxy.write_long_characteristic(
                id,
                offset,
                &write_value,
                WriteOptions { reliable_mode: Some(reliable_mode) },
            ),
            None => fx_err_and_bail!(&with_line!(tag), "Central proxy not available."),
        };

        let status = write_long_characteristic
            .await
            .map_err(|_| Sl4fError::new("Failed to send message"))?;

        match status.error {
            Some(e) => {
                let err_msg =
                    format!("Failed to write long characteristic: {}", Sl4fError::from(*e));
                fx_err_and_bail!(&with_line!(tag), err_msg)
            }
            None => Ok(()),
        }
    }

    pub async fn gattc_write_char_by_id_without_response(
        &self,
        id: u64,
        write_value: Vec<u8>,
    ) -> Result<(), Error> {
        let tag = "GattClientFacade::gattc_write_char_by_id_without_response";

        match &self.inner.read().active_proxy {
            Some(proxy) => proxy
                .write_characteristic_without_response(id, &write_value)
                .map_err(|_| Sl4fError::new("Failed to send message").into()),
            None => fx_err_and_bail!(&with_line!(tag), "Central proxy not available."),
        }
    }

    pub async fn gattc_read_char_by_id(&self, id: u64) -> Result<Vec<u8>, Error> {
        let tag = "GattClientFacade::gattc_read_char_by_id";

        let read_characteristic = match &self.inner.read().active_proxy {
            Some(proxy) => proxy.read_characteristic(id),
            None => fx_err_and_bail!(&with_line!(tag), "Central proxy not available."),
        };

        let (status, value) =
            read_characteristic.await.map_err(|_| Sl4fError::new("Failed to send message"))?;

        match status.error {
            Some(e) => {
                let err_msg = format!("Failed to read characteristic: {}", Sl4fError::from(*e));
                fx_err_and_bail!(&with_line!(tag), err_msg)
            }
            None => Ok(value),
        }
    }

    pub async fn gattc_read_char_by_type(
        &self,
        raw_uuid: String,
    ) -> Result<Vec<SerializableReadByTypeResult>, Error> {
        let tag = "GattClientFacade::gattc_read_char_by_type";

        let uuid = match Uuid::from_str(&raw_uuid) {
            Ok(uuid) => uuid,
            Err(e) => {
                fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Unable to convert to Uuid: {:?}", e)
                );
            }
        };

        let mut fidl_uuid = fidl_fuchsia_bluetooth::Uuid::from(uuid);

        match &self.inner.read().active_proxy {
            Some(proxy) => match proxy.read_by_type(&mut fidl_uuid).await {
                Ok(value) => {
                    let responses = value.unwrap();
                    let mut read_by_type_response_list = Vec::new();
                    for response in responses {
                        read_by_type_response_list
                            .push(SerializableReadByTypeResult::new(&response));
                    }
                    Ok(read_by_type_response_list)
                }
                Err(e) => {
                    let err_msg = format!("Failed to read characteristic by type: {}", e);
                    fx_err_and_bail!(&with_line!(tag), err_msg)
                }
            },
            None => fx_err_and_bail!(&with_line!(tag), "Central proxy not available."),
        }
    }

    pub async fn gattc_read_long_char_by_id(
        &self,
        id: u64,
        offset: u16,
        max_bytes: u16,
    ) -> Result<Vec<u8>, Error> {
        let tag = "GattClientFacade::gattc_read_long_char_by_id";

        let read_long_characteristic = match &self.inner.read().active_proxy {
            Some(proxy) => proxy.read_long_characteristic(id, offset, max_bytes),
            None => fx_err_and_bail!(&with_line!(tag), "Central proxy not available."),
        };

        let (status, value) =
            read_long_characteristic.await.map_err(|_| Sl4fError::new("Failed to send message"))?;

        match status.error {
            Some(e) => {
                let err_msg =
                    format!("Failed to read long characteristic: {}", Sl4fError::from(*e));
                fx_err_and_bail!(&with_line!(tag), err_msg)
            }
            None => Ok(value),
        }
    }

    pub async fn gattc_read_desc_by_id(&self, id: u64) -> Result<Vec<u8>, Error> {
        let tag = "GattClientFacade::gattc_read_desc_by_id";
        let read_descriptor = match &self.inner.read().active_proxy {
            Some(proxy) => proxy.read_descriptor(id),
            None => fx_err_and_bail!(&with_line!(tag), "Central proxy not available."),
        };

        let (status, value) =
            read_descriptor.await.map_err(|_| Sl4fError::new("Failed to send message"))?;

        match status.error {
            Some(e) => {
                let err_msg = format!("Failed to read descriptor: {}", Sl4fError::from(*e));
                fx_err_and_bail!(&with_line!(tag), err_msg)
            }
            None => Ok(value),
        }
    }

    pub async fn gattc_read_long_desc_by_id(
        &self,
        id: u64,
        offset: u16,
        max_bytes: u16,
    ) -> Result<Vec<u8>, Error> {
        let tag = "GattClientFacade::gattc_read_long_desc_by_id";
        let read_long_descriptor = match &self.inner.read().active_proxy {
            Some(proxy) => proxy.read_long_descriptor(id, offset, max_bytes),
            None => fx_err_and_bail!(&with_line!(tag), "Central proxy not available."),
        };

        let (status, value) =
            read_long_descriptor.await.map_err(|_| Sl4fError::new("Failed to send message"))?;

        match status.error {
            Some(e) => {
                let err_msg = format!("Failed to read long descriptor: {}", Sl4fError::from(*e));
                fx_err_and_bail!(&with_line!(tag), err_msg)
            }
            None => Ok(value),
        }
    }

    pub async fn gattc_write_desc_by_id(&self, id: u64, write_value: Vec<u8>) -> Result<(), Error> {
        let tag = "GattClientFacade::gattc_write_desc_by_id";

        let write_descriptor = match &self.inner.read().active_proxy {
            Some(proxy) => proxy.write_descriptor(id, &write_value),
            None => fx_err_and_bail!(&with_line!(tag), "Central proxy not available."),
        };

        let status =
            write_descriptor.await.map_err(|_| Sl4fError::new("Failed to send message"))?;

        match status.error {
            Some(e) => {
                let err_msg = format!("Failed to write to descriptor: {}", Sl4fError::from(*e));
                fx_err_and_bail!(&with_line!(tag), err_msg)
            }
            None => Ok(()),
        }
    }

    pub async fn gattc_write_long_desc_by_id(
        &self,
        id: u64,
        offset: u16,
        write_value: Vec<u8>,
    ) -> Result<(), Error> {
        let tag = "GattClientFacade::gattc_write_long_desc_by_id";

        let write_long_descriptor = match &self.inner.read().active_proxy {
            Some(proxy) => proxy.write_long_descriptor(id, offset, &write_value),
            None => fx_err_and_bail!(&with_line!(tag), "Central proxy not available."),
        };

        let status =
            write_long_descriptor.await.map_err(|_| Sl4fError::new("Failed to send message"))?;

        match status.error {
            Some(e) => {
                let err_msg = format!("Failed to write long descriptor: {}", Sl4fError::from(*e));
                fx_err_and_bail!(&with_line!(tag), err_msg)
            }
            None => Ok(()),
        }
    }

    pub async fn gattc_toggle_notify_characteristic(
        &self,
        id: u64,
        value: bool,
    ) -> Result<(), Error> {
        let tag = "GattClientFacade::gattc_toggle_notify_characteristic";

        let notify_characteristic = match &self.inner.read().active_proxy {
            Some(proxy) => proxy.notify_characteristic(id, value),
            None => fx_err_and_bail!(&with_line!(tag), "Central proxy not available."),
        };

        let status =
            notify_characteristic.await.map_err(|_| Sl4fError::new("Failed to send message"))?;

        match status.error {
            Some(e) => {
                let err_msg = format!("Failed to enable notifications: {}", Sl4fError::from(*e));
                fx_err_and_bail!(&with_line!(tag), err_msg)
            }
            None => {}
        };
        Ok(())
    }

    pub async fn list_services(&self, id: String) -> Result<Vec<ServiceInfo>, Error> {
        let tag = "GattClientFacade::list_services";
        let client_proxy = self.get_client_from_peripherals(id);

        match client_proxy {
            Some(c) => {
                let (status, services) = c.list_services(None).await?;
                match status.error {
                    None => {
                        fx_log_info!(tag: &with_line!(tag), "Found services: {:?}", services);
                        Ok(services)
                    }
                    Some(e) => {
                        let err_msg = format!(
                            "Error found while listing services: {:?}",
                            Sl4fError::from(*e)
                        );
                        fx_err_and_bail!(&with_line!(tag), err_msg)
                    }
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
        let tag = "GattClientFacade::update_peripheral_id";
        if self.inner.read().peripheral_ids.contains_key(id) {
            fx_log_warn!(tag: &with_line!(tag), "Overwriting existing id: {}", id);
            self.inner.write().peripheral_ids.insert(id.clone(), client);
        } else {
            fx_log_info!(tag: &with_line!(tag), "Added {:?} to peripheral ids", id);
            self.inner.write().peripheral_ids.insert(id.clone(), client);
        }

        fx_log_info!(
            tag: &with_line!(tag),
            "Peripheral ids: {:?}",
            self.inner.read().peripheral_ids
        );
    }

    pub fn remove_peripheral_id(&self, id: &String) {
        let tag = "GattClientFacade::remove_peripheral_id";
        self.inner.write().peripheral_ids.remove(id);
        fx_log_info!(
            tag: &with_line!(tag),
            "After removing peripheral id: {:?}",
            self.inner.read().peripheral_ids
        );
    }

    // Update the central proxy if none exists, otherwise raise error
    // If no proxy exists, set up central server to listen for events. This central listener will
    // wake up any wakers who may be interested in RemoteDevices discovered
    pub fn set_central_proxy(inner: Arc<RwLock<InnerGattClientFacade>>) {
        let tag = "GattClientFacade::set_central_proxy";
        let mut central_modified = false;
        let new_central = match inner.read().central.clone() {
            Some(c) => {
                fx_log_warn!(tag: &with_line!(tag), "Current central: {:?}.", c);
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
            fasync::Task::spawn(GattClientFacade::listen_central_events(inner.clone())).detach()
        }
    }

    // Update the devices dictionary with a discovered RemoteDevice
    pub fn update_devices(
        inner: &Arc<RwLock<InnerGattClientFacade>>,
        id: String,
        device: RemoteDevice,
    ) {
        let tag = "GattClientFacade::update_devices";
        if inner.read().devices.contains_key(&id) {
            fx_log_warn!(tag: &with_line!(tag), "Already discovered: {:?}", id);
        } else {
            inner.write().devices.insert(id, device);
        }
    }

    pub fn listen_central_events(
        inner: Arc<RwLock<InnerGattClientFacade>>,
    ) -> impl Future<Output = ()> {
        let tag = "GattClientFacade::listen_central_events";
        let evt_stream = match inner.read().central.clone() {
            Some(c) => c.take_event_stream(),
            None => panic!("No central created!"),
        };

        evt_stream
            .map_ok(move |evt| {
                match evt {
                    CentralEvent::OnScanStateChanged { scanning } => {
                        fx_log_info!(tag: &with_line!(tag), "Scan state changed: {:?}", scanning);
                    }
                    CentralEvent::OnDeviceDiscovered { device } => {
                        let id = device.identifier.clone();
                        let name = device.advertising_data.as_ref().map(|adv| &adv.name);
                        // Update the device discovered list
                        fx_log_info!(
                            tag: &with_line!(tag),
                            "Device discovered: id: {:?}, name: {:?}",
                            id,
                            name
                        );
                        GattClientFacade::update_devices(&inner, id, device);

                        // In the event that we need to short-circuit the stream, wake up all
                        // wakers in the host_requests Slab
                        for waker in &inner.read().host_requests {
                            waker.1.wake_by_ref();
                        }
                    }
                    CentralEvent::OnPeripheralDisconnected { identifier } => {
                        fx_log_info!(tag: &with_line!(tag), "Peer disconnected: {:?}", identifier);
                    }
                }
            })
            .try_collect::<()>()
            .unwrap_or_else(|e| {
                fx_log_err!(
                    tag: &with_line!("GattClientFacade::listen_central_events"),
                    "Failed to subscribe to BLE Central events: {:?}",
                    e
                )
            })
    }

    pub async fn connect_peripheral(&self, id: String) -> Result<(), Error> {
        let tag = "GattClientFacade::connect_peripheral";
        // Set the central proxy if necessary
        GattClientFacade::set_central_proxy(self.inner.clone());

        // TODO(fxbug.dev/875): Move to private method?
        // Create server endpoints
        let (proxy, server_end) = match fidl::endpoints::create_proxy() {
            Err(e) => {
                let err_msg = format!("Failed to create proxy endpoint: {:?}", e);
                fx_err_and_bail!(&with_line!(tag), err_msg)
            }
            Ok(x) => x,
        };
        let mut identifier = id.clone();
        self.update_peripheral_id(&identifier, proxy);
        match &self.inner.read().central {
            Some(c) => {
                let conn_opts =
                    ConnectionOptions { bondable_mode: Some(true), service_filter: None };
                let status = c.connect_peripheral(&mut identifier, conn_opts, server_end).await?;
                match status.error {
                    Some(e) => {
                        let err_msg =
                            format!("Failed to connect to peripheral: {:?}", Sl4fError::from(*e));
                        fx_err_and_bail!(&with_line!(tag), err_msg)
                    }
                    None => {}
                }
            }
            None => fx_err_and_bail!(&with_line!(tag), "No central proxy created."),
        };
        Ok(())
    }

    pub async fn disconnect_peripheral(&self, id: String) -> Result<(), Error> {
        let tag = "GattClientFacade::disconnect_peripheral";
        match &self.inner.read().central {
            Some(c) => {
                let status = c.disconnect_peripheral(&id).await?;
                match status.error {
                    None => {}
                    Some(e) => {
                        fx_log_err!(tag: &with_line!(tag), "Failed to disconnect: {:?}", e);
                        bail!("Failed to disconnect: {:?}", e)
                    }
                }
            }
            None => fx_err_and_bail!(&with_line!(tag), "Failed to disconnect from perpheral."),
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
    // TODO(fxbug.dev/869): Return list of RemoteDevices (unsupported right now
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
        let tag = "GattClientFacade::print";
        fx_log_info!(
            tag: &with_line!(tag),
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
