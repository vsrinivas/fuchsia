// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_bluetooth_le::{
    AdvertisedPeripheralMarker, AdvertisedPeripheralRequest, AdvertisedPeripheralRequestStream,
    AdvertisingParameters, ConnectionProxy, PeripheralMarker, PeripheralProxy,
};
use fuchsia_async as fasync;
use fuchsia_bluetooth::types::PeerId;
use fuchsia_component as app;
use futures::{pin_mut, select, FutureExt, StreamExt};
use parking_lot::RwLock;
use serde_json::Value;
use std::convert::TryInto;
use std::{collections::HashMap, sync::Arc};
use tracing::{debug, error, info, warn};

// Sl4f-Constants and Ble advertising related functionality
use crate::bluetooth::types::FacadeArg;
use crate::common_utils::common::macros::with_line;

#[derive(Debug)]
struct Connection(ConnectionProxy, fasync::Task<()>);

#[derive(Debug)]
struct InnerBleAdvertiseFacade {
    /// Advertised peripheral server of the facade, only one advertisement at a time.
    advertise_task: Option<fasync::Task<()>>,

    // Active connections.
    connections: HashMap<PeerId, Connection>,

    ///PeripheralProxy used for Bluetooth Connections
    peripheral: Option<PeripheralProxy>,
}

/// Starts and stops device BLE advertisement(s).
/// Note this object is shared among all threads created by server.
#[derive(Debug)]
pub struct BleAdvertiseFacade {
    inner: Arc<RwLock<InnerBleAdvertiseFacade>>,
}

impl BleAdvertiseFacade {
    pub fn new() -> BleAdvertiseFacade {
        BleAdvertiseFacade {
            inner: Arc::new(RwLock::new(InnerBleAdvertiseFacade {
                advertise_task: None,
                connections: HashMap::new(),
                peripheral: None,
            })),
        }
    }

    fn set_advertise_task(
        inner: &Arc<RwLock<InnerBleAdvertiseFacade>>,
        task: Option<fasync::Task<()>>,
    ) {
        let tag = "BleAdvertiseFacade::set_advertise_task";
        if task.is_some() {
            info!(tag = &with_line!(tag), "Assigned new advertise task");
        } else if inner.read().advertise_task.is_some() {
            info!(tag = &with_line!(tag), "Cleared advertise task");
        }
        inner.write().advertise_task = task;
    }

    pub fn print(&self) {
        let adv_status = match &self.inner.read().advertise_task {
            Some(_) => "Valid",
            None => "None",
        };
        info!(tag = &with_line!("BleAdvertiseFacade::print"),
            %adv_status,
            peripheral = ?self.get_peripheral_proxy(),
            "BleAdvertiseFacade",
        );
    }

    // Set the peripheral proxy only if none exists, otherwise, use existing
    pub fn set_peripheral_proxy(&self) {
        let tag = "BleAdvertiseFacade::set_peripheral_proxy";

        let new_peripheral = match self.inner.read().peripheral.clone() {
            Some(p) => {
                warn!(tag = &with_line!(tag), current_peripheral = ?p);
                Some(p)
            }
            None => {
                let peripheral_svc: PeripheralProxy =
                    app::client::connect_to_protocol::<PeripheralMarker>()
                        .context("Failed to connect to BLE Peripheral service.")
                        .unwrap();
                Some(peripheral_svc)
            }
        };

        self.inner.write().peripheral = new_peripheral
    }

    /// Start BLE advertisement
    ///
    /// # Arguments
    /// * `args`: A JSON input representing advertisement parameters.
    pub async fn start_adv(&self, args: Value) -> Result<(), Error> {
        self.set_peripheral_proxy();
        let parameters: AdvertisingParameters = FacadeArg::new(args).try_into()?;
        let periph = &self.inner.read().peripheral.clone();
        match &periph {
            Some(p) => {
                // Clear any existing advertisement.
                BleAdvertiseFacade::set_advertise_task(&self.inner, None);

                let advertise_task = fasync::Task::spawn(BleAdvertiseFacade::advertise(
                    self.inner.clone(),
                    p.clone(),
                    parameters,
                ));
                info!(tag = "start_adv", "Started advertising");
                BleAdvertiseFacade::set_advertise_task(&self.inner, Some(advertise_task));
                Ok(())
            }
            None => {
                error!(tag = "start_adv", "No peripheral created.");
                return Err(format_err!("No peripheral proxy created."));
            }
        }
    }

    fn process_new_connection(
        inner: Arc<RwLock<InnerBleAdvertiseFacade>>,
        proxy: ConnectionProxy,
        peer_id: PeerId,
    ) {
        let tag = "BleAdvertiseFacade::process_new_connection";

        let mut stream = proxy.take_event_stream();

        let inner_clone = inner.clone();
        let stream_fut = async move {
            while let Some(event) = stream.next().await {
                match event {
                    Ok(_) => {
                        debug!(tag = &with_line!(tag), "ignoring event for Connection");
                    }
                    Err(err) => {
                        info!(tag = &with_line!(tag), "Connection ({}) error: {:?}", peer_id, err);
                    }
                }
            }
            info!(tag = &with_line!(tag), "peer {} disconnected", peer_id);
            inner_clone.write().connections.remove(&peer_id);
        };
        let event_task = fasync::Task::spawn(stream_fut);
        inner.write().connections.insert(peer_id, Connection(proxy, event_task));
    }

    async fn process_advertised_peripheral_stream(
        inner: Arc<RwLock<InnerBleAdvertiseFacade>>,
        mut stream: AdvertisedPeripheralRequestStream,
    ) {
        let tag = "BleAdvertiseFacade::process_advertised_peripheral_stream";
        while let Some(request) = stream.next().await {
            match request {
                Ok(AdvertisedPeripheralRequest::OnConnected { peer, connection, responder }) => {
                    if let Err(err) = responder.send() {
                        warn!(
                            tag = &with_line!(tag),
                            "error sending response to AdvertisedPeripheral::OnConnected: {}", err
                        );
                    }

                    let proxy = match connection.into_proxy() {
                        Ok(proxy) => proxy,
                        Err(_) => {
                            warn!(
                                tag = &with_line!(tag),
                                "error creating Connection proxy, dropping Connection"
                            );
                            continue;
                        }
                    };
                    let peer_id: PeerId = peer.id.unwrap().into();
                    BleAdvertiseFacade::process_new_connection(inner.clone(), proxy, peer_id);
                }
                Err(err) => {
                    info!(tag = &with_line!(tag), "AdvertisedPeripheral error: {:?}", err);
                }
            }
        }
        info!(tag = &with_line!(tag), "AdvertisedPeripheral closed, stopping advertising");
        BleAdvertiseFacade::set_advertise_task(&inner, None);
    }

    async fn advertise(
        inner: Arc<RwLock<InnerBleAdvertiseFacade>>,
        peripheral: PeripheralProxy,
        parameters: AdvertisingParameters,
    ) {
        let tag = "BleAdvertiseFacade::advertise";
        let (client_end, server_request_stream) =
            match create_request_stream::<AdvertisedPeripheralMarker>() {
                Ok(value) => value,
                Err(_) => return,
            };

        // advertise() only returns after advertising has been terminated, so we can't await here.
        let advertise_fut = peripheral.advertise(parameters, client_end);

        let server_fut = BleAdvertiseFacade::process_advertised_peripheral_stream(
            inner.clone(),
            server_request_stream,
        );

        let advertise_fut_fused = advertise_fut.fuse();
        let server_fut_fused = server_fut.fuse();
        pin_mut!(advertise_fut_fused, server_fut_fused);
        select! {
             result = advertise_fut_fused => {
                info!(tag = &with_line!(tag), "advertise() returned with result {:?}", result);
             }
             _ = server_fut_fused => {
                info!(tag = &with_line!(tag), "AdvertisedPeripheral closed");
             }
        };

        // Stop advertising.
        inner.write().advertise_task.take();
    }

    pub fn stop_adv(&self) {
        info!(tag = &with_line!("BleAdvertiseFacade::stop_adv"), "Stop advertising");
        BleAdvertiseFacade::set_advertise_task(&self.inner, None);
    }

    pub fn get_peripheral_proxy(&self) -> Option<PeripheralProxy> {
        self.inner.read().peripheral.clone()
    }

    // Close peripheral proxy
    pub fn cleanup_peripheral_proxy(&self) {
        self.inner.write().peripheral = None;
    }

    // Cancel all tasks and close all protocols.
    pub fn cleanup(&self) {
        self.inner.write().connections.clear();
        self.stop_adv();
        self.cleanup_peripheral_proxy();
    }
}
