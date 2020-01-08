// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Coordinate status between different devices in the current netemul test.

use anyhow::{format_err, Context as _, Error};
use futures::StreamExt;
use log::error;

use fidl_fuchsia_netemul_sync as nsync;
use fuchsia_component as component;

use crate::device;

const BUS_NAME: &str = "nmh::nat::bus";

/// Publishes status of the current device and waits for status updates from other devices in the
/// netemul test.
pub struct SyncManager {
    device: device::Type,
    proxy: nsync::BusProxy,
    events: nsync::BusEventStream,
    status: [device::Status; 3],
}

impl SyncManager {
    /// Attach to the SyncManager, subscribing to the bus as the given device. Waits for all other
    /// devices to attach as well before returning a SyncManager object.
    pub async fn attach(device: device::Type) -> Result<Self, Error> {
        let sync_mgr = component::client::connect_to_service::<nsync::SyncManagerMarker>()
            .context("unable to connect to sync manager")?;
        let (proxy, server_end) = fidl::endpoints::create_proxy::<nsync::BusMarker>()
            .context("error creating bus endpoints")?;
        sync_mgr
            .bus_subscribe(BUS_NAME, &device.to_string(), server_end)
            .context("error subscribing to bus")?;
        let events = proxy.take_event_stream();
        let mut mgr = SyncManager { device, proxy, events, status: [device::STATUS_UNKNOWN; 3] };
        mgr.wait_for_status_or_error(device::STATUS_ATTACHED).await?;
        Ok(mgr)
    }

    /// Status of the device with the given name.
    fn status_by_name(&mut self, name: String) -> &mut device::Status {
        &mut self.status[device::Type::from(name) as usize]
    }

    /// Handle the event received on the sync bus.
    fn handle_event(&mut self, event: nsync::BusEvent) {
        match event {
            nsync::BusEvent::OnClientAttached { client } => {
                self.status_by_name(client).add(device::STATUS_ATTACHED)
            }
            nsync::BusEvent::OnClientDetached { client } => {
                self.status_by_name(client).remove(device::STATUS_ATTACHED)
            }
            nsync::BusEvent::OnBusData {
                data: nsync::Event { code: Some(code), message: Some(message), arguments: _ },
            } => self.status_by_name(message).add(code.into()),
            event @ _ => error!("unrecognized sync bus event: {:?}", event),
        }
    }

    /// Check that all other devices' last known status contains the given status. If any device
    /// has an ERROR status, this function returns an error. Otherwise returns true if all devices
    /// match the expected status, or false if any of them does not.
    fn others_last_known_status_or_error(&self, expected: device::Status) -> Result<bool, Error> {
        self.device.others().iter().try_fold(true, |acc, &dev| {
            let status = self.status[dev as usize];
            if status.contains(device::STATUS_ERROR) {
                Err(format_err!(
                    "error on device {}, expected status {:?}",
                    dev.to_string(),
                    expected
                ))
            } else {
                Ok(acc && status.contains(expected))
            }
        })
    }

    /// Waits for all other devices to reach the expected status. If any device reaches an ERROR
    /// state, returns immediately with an error.
    pub async fn wait_for_status_or_error(
        &mut self,
        expected: device::Status,
    ) -> Result<(), Error> {
        loop {
            if self.others_last_known_status_or_error(expected)? {
                return Ok(());
            }
            match self.events.next().await {
                Some(Ok(event)) => {
                    self.handle_event(event);
                }
                err @ _ => error!("error receiving from sync event bus: {:?}", err),
            }
        }
    }

    /// Publishes the status of the current device.
    pub async fn publish_status(&mut self, status: device::Status) -> Result<(), Error> {
        self.status[self.device as usize] = status;
        let name: String = self.device.to_string();
        self.proxy
            .ensure_publish(nsync::Event {
                code: Some(status.into()),
                message: Some(name),
                arguments: None,
            })
            .await
            .map_err(|e| e.into())
    }
}
