// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use battery_client::BatteryClient;
use bt_le_battery_service_config::Config;
use fidl::endpoints::{create_request_stream, RequestStream, Responder};
use fidl_fuchsia_bluetooth_gatt2 as gatt;
use fuchsia_bluetooth::types::{PeerId, Uuid};
use fuchsia_component::client::connect_to_protocol;
use futures::stream::{StreamExt, TryStreamExt};
use futures::try_join;
use parking_lot::Mutex;
use std::collections::HashSet;
use std::str::FromStr;
use tracing::{info, warn};

/// Arbitrary Handle assigned to the Battery Service.
const BATTERY_SERVICE_HANDLE: gatt::ServiceHandle = gatt::ServiceHandle { value: 10 };
/// Fixed Handle assigned to the Battery characteristic.
const BATTERY_CHARACTERISTIC_HANDLE: gatt::Handle = gatt::Handle { value: 1 };
const BATTERY_SERVICE_UUID: &str = "0000180f-0000-1000-8000-00805f9b34fb";
const BATTERY_LEVEL_UUID: &str = "00002A19-0000-1000-8000-00805f9b34fb";

/// Struct to manage all the shared state of the tool.
struct BatteryState {
    inner: Mutex<BatteryStateInner>,
}

impl BatteryState {
    pub fn new(service: gatt::LocalServiceControlHandle) -> BatteryState {
        BatteryState {
            inner: Mutex::new(BatteryStateInner { level: 0, service, peers: HashSet::new() }),
        }
    }

    /// Add a new peer to the set of peers interested in the battery level change notifications.
    pub fn add_peer(&self, peer_id: PeerId) {
        let _ = self.inner.lock().peers.insert(peer_id);
    }

    /// Remove a peer from the set of peers interested in notifications.
    pub fn remove_peer(&self, peer_id: &PeerId) {
        let _ = self.inner.lock().peers.remove(peer_id);
    }

    /// Get the last reported level of the battery as a percentage in [0, 100].
    pub fn get_level(&self) -> u8 {
        self.inner.lock().level
    }

    /// Set the level to the given value and notify any interested peers
    /// of the change.
    pub fn set_level(&self, level: u8) -> Result<(), Error> {
        let mut inner = self.inner.lock();
        if inner.level != level {
            info!("Battery percentage changed ({}%)", level);
            let params = gatt::ValueChangedParameters {
                peer_ids: Some(inner.peers.iter().cloned().map(Into::into).collect()),
                handle: Some(BATTERY_CHARACTERISTIC_HANDLE),
                value: Some(vec![level]),
                ..gatt::ValueChangedParameters::EMPTY
            };
            inner.service.send_on_notify_value(params)?;
        }
        inner.level = level;
        Ok(())
    }
}

/// Inner data fields used for the `BatteryState` struct.
struct BatteryStateInner {
    /// The current battery percentage. In the range [0, 100].
    level: u8,

    /// The control handle used to send GATT notifications.
    service: gatt::LocalServiceControlHandle,

    /// Set of remote peers that have subscribed to GATT battery notifications.
    peers: HashSet<PeerId>,
}

/// Handle a stream of incoming gatt battery service requests.
/// Returns when the channel backing the stream closes or an error occurs while handling requests.
async fn gatt_service_delegate(
    state: &BatteryState,
    mut stream: gatt::LocalServiceRequestStream,
) -> Result<(), Error> {
    use gatt::LocalServiceRequest;
    while let Some(request) = stream.try_next().await.context("error running service delegate")? {
        match request {
            LocalServiceRequest::ReadValue { responder, .. } => {
                let battery_level = state.get_level();
                let _ = responder.send(&mut Ok(vec![battery_level]));
            }
            LocalServiceRequest::WriteValue { responder, .. } => {
                // Writing to the battery level characteristic is not permitted.
                let _ = responder.send(&mut Err(gatt::Error::WriteRequestRejected));
            }
            LocalServiceRequest::CharacteristicConfiguration {
                peer_id, notify, responder, ..
            } => {
                let peer_id = peer_id.into();
                info!("Peer {} configured characteristic (notify: {})", peer_id, notify);
                if notify {
                    state.add_peer(peer_id);
                } else {
                    state.remove_peer(&peer_id);
                }
                let _ = responder.send();
            }
            LocalServiceRequest::ValueChangedCredit { .. } => {}
            LocalServiceRequest::PeerUpdate { responder, .. } => {
                // Per FIDL docs, this can be safely ignored
                responder.drop_without_shutdown();
            }
        }
    }
    warn!("GATT Battery service was closed");
    Ok(())
}

/// Watches and saves updates from the local battery service.
async fn battery_manager_watcher(
    state: &BatteryState,
    mut battery_client: BatteryClient,
) -> Result<(), Error> {
    while let Some(update) = battery_client.next().await {
        if let Some(battery_level) = update.map(|u| u.level())? {
            state.set_level(battery_level)?;
        }
    }

    warn!("BatteryClient was closed; battery level no longer available.");
    Ok(())
}

#[fuchsia::main(logging_tags = ["bt-le-battery-service"])]
async fn main() -> Result<(), Error> {
    let config = Config::take_from_startup_handle();
    let security = match config.security.as_str() {
        "none" => gatt::SecurityRequirements::EMPTY,
        "enc" => gatt::SecurityRequirements {
            encryption_required: Some(true),
            ..gatt::SecurityRequirements::EMPTY
        },
        "auth" => gatt::SecurityRequirements {
            encryption_required: Some(true),
            authentication_required: Some(true),
            ..gatt::SecurityRequirements::EMPTY
        },
        other => return Err(format_err!("invalid security value: {}", other)),
    };
    info!("Starting LE Battery service with security: {:?}", security);

    // Connect to the gatt2.Server protocol to publish the service.
    let gatt_server = connect_to_protocol::<gatt::Server_Marker>()?;
    let (service_client, service_stream) = create_request_stream::<gatt::LocalServiceMarker>()
        .context("Can't create LocalService endpoints")?;
    let service_notification_handle = service_stream.control_handle();

    // Connect to the battery service and initialize the shared state.
    let battery_client = BatteryClient::create()?;
    let state = BatteryState::new(service_notification_handle);

    // Build a GATT Battery service.
    let characteristic = gatt::Characteristic {
        handle: Some(BATTERY_CHARACTERISTIC_HANDLE),
        type_: Uuid::from_str(BATTERY_LEVEL_UUID).ok().map(Into::into),
        properties: Some(
            gatt::CharacteristicPropertyBits::READ | gatt::CharacteristicPropertyBits::NOTIFY,
        ),
        permissions: Some(gatt::AttributePermissions {
            read: Some(security.clone()),
            update: Some(security),
            ..gatt::AttributePermissions::EMPTY
        }),
        ..gatt::Characteristic::EMPTY
    };
    let service_info = gatt::ServiceInfo {
        handle: Some(BATTERY_SERVICE_HANDLE),
        kind: Some(gatt::ServiceKind::Primary),
        type_: Uuid::from_str(BATTERY_SERVICE_UUID).ok().map(Into::into),
        characteristics: Some(vec![characteristic]),
        ..gatt::ServiceInfo::EMPTY
    };

    // Publish the local gatt service delegate with the gatt service.
    gatt_server
        .publish_service(service_info, service_client)
        .await?
        .map_err(|e| format_err!("Failed to publish battery service to gatt server: {:?}", e))?;
    info!("Published Battery Service to local GATT database.");

    // Start the gatt service delegate and battery watcher server.
    let service_delegate = gatt_service_delegate(&state, service_stream);
    let battery_watcher = battery_manager_watcher(&state, battery_client);
    try_join!(service_delegate, battery_watcher).map(|((), ())| ())
}
