// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fidl_clone::FIDLClone;
use crate::tests::fakes::base::Service;
use crate::tests::fakes::fake_hanging_get_handler::{HangingGetHandler, Sender};
use crate::tests::fakes::fake_hanging_get_types::ChangedPeers;
use anyhow::{format_err, Context, Error};
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fidl_fuchsia_bluetooth::{
    Address, AddressType, Appearance, DeviceClass, PeerId, MAJOR_DEVICE_CLASS_PHONE,
};
use fidl_fuchsia_bluetooth_sys::{
    AccessMarker, AccessRequest, AccessWatchPeersResponder, Peer, TechnologyType,
};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::channel::mpsc::UnboundedSender;
use futures::lock::Mutex;
use futures::TryStreamExt;
use std::sync::Arc;

const PEER_TEMPLATE: Peer = Peer {
    id: Some(PeerId { value: 1 }),
    address: Some(Address { type_: AddressType::Public, bytes: [30, 65, 236, 111, 196, 58] }),
    technology: Some(TechnologyType::Classic),
    connected: Some(true),
    bonded: Some(true),
    name: None,
    appearance: Some(Appearance::Phone),
    device_class: Some(DeviceClass { value: MAJOR_DEVICE_CLASS_PHONE }),
    rssi: Some(-70),
    tx_power: Some(38),
    services: None,
    le_services: None,
    bredr_services: None,
};

// Sender trait for the fake hanging get handler to send a response back.
impl Sender<ChangedPeers> for AccessWatchPeersResponder {
    fn send_response(self, data: ChangedPeers) {
        self.send(&mut data.0.into_iter(), &mut Vec::new().into_iter())
            .context("Failed to send response back for watch on bluetooth_service fake")
            .unwrap();
    }
}

/// An implementation of the Bluetooth connection state manager for tests.
pub struct BluetoothService {
    // Fake hanging get handler. Allows the service to mock the return of the watch
    // on a connection change.
    hanging_get_handler: Arc<Mutex<HangingGetHandler<ChangedPeers, AccessWatchPeersResponder>>>,

    // Sender on which the fake service will send back peer updates.
    on_update_sender: Arc<Mutex<UnboundedSender<ChangedPeers>>>,
}

impl BluetoothService {
    pub fn new(
        hanging_get_handler: Arc<Mutex<HangingGetHandler<ChangedPeers, AccessWatchPeersResponder>>>,
        on_update_sender: UnboundedSender<ChangedPeers>,
    ) -> Self {
        Self { hanging_get_handler, on_update_sender: Arc::new(Mutex::new(on_update_sender)) }
    }

    /// Simulate connecting a single peer.
    pub async fn connect(&self, peer_id: PeerId, is_oobe_connection: bool) -> Result<(), Error> {
        // Create peer to send as a newly connected peer.
        let mut peer = PEER_TEMPLATE.clone();
        if is_oobe_connection {
            peer.technology = Some(TechnologyType::LowEnergy);
        }
        peer.id = Some(peer_id);
        let mut added_peers = Vec::new();
        added_peers.push(peer);

        // Send peer state over unbounded send connection.
        match self.on_update_sender.lock().await.unbounded_send((added_peers, Vec::new())) {
            Ok(_) => Ok(()),
            Err(e) => Err(format_err!("Failed to connect new bluetooth peer: {}", e)),
        }
    }

    /// Simulate disconnecting a single peer.
    pub async fn disconnect(&self, peer_id: PeerId, is_oobe_connection: bool) -> Result<(), Error> {
        let mut peer = PEER_TEMPLATE.clone();
        if is_oobe_connection {
            peer.technology = Some(TechnologyType::LowEnergy);
        }
        peer.id = Some(peer_id);
        peer.connected = Some(false);
        let mut removed_peers = Vec::new();
        removed_peers.push(peer);

        // Send peer state over unbounded send connection.
        match self.on_update_sender.lock().await.unbounded_send((removed_peers, Vec::new())) {
            Ok(_) => Ok(()),
            Err(e) => Err(format_err!("Failed to disconnect peer: {}", e)),
        }
    }
}

impl Service for BluetoothService {
    fn can_handle_service(&self, service_name: &str) -> bool {
        return service_name == AccessMarker::NAME;
    }

    fn process_stream(&mut self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("unsupported"));
        }

        let mut bluetooth_stream = ServerEnd::<AccessMarker>::new(channel).into_stream()?;
        let hanging_get_handler = self.hanging_get_handler.clone();

        fasync::Task::spawn(async move {
            while let Some(req) = bluetooth_stream.try_next().await.unwrap() {
                match req {
                    AccessRequest::WatchPeers { responder } => {
                        hanging_get_handler.lock().await.watch(responder).await;
                    }
                    _ => {}
                }
            }
        })
        .detach();

        Ok(())
    }
}
