// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_bredr as bredr;
use fidl_fuchsia_bluetooth_deviceid as di;
use futures::{channel::mpsc, select, StreamExt};
use tracing::{info, warn};

use crate::error::Error;

/// The server that manages the current set of Device Identification advertisements.
pub struct DeviceIdServer {
    /// The maximum number of concurrent DI advertisements this server supports.
    _max_advertisements: usize,
    /// The connection to the upstream BR/EDR Profile server.
    _profile: bredr::ProfileProxy,
    /// Incoming client connections for the `DeviceIdentification` protocol.
    device_id_clients: mpsc::Receiver<di::DeviceIdentificationRequestStream>,
    /// `DeviceIdentification` protocol requests from connected clients.
    device_id_requests: futures::stream::SelectAll<di::DeviceIdentificationRequestStream>,
}

impl DeviceIdServer {
    pub fn new(
        _max_advertisements: usize,
        _profile: bredr::ProfileProxy,
        device_id_clients: mpsc::Receiver<di::DeviceIdentificationRequestStream>,
    ) -> Self {
        Self {
            _max_advertisements,
            _profile,
            device_id_clients,
            device_id_requests: futures::stream::SelectAll::new(),
        }
    }

    pub async fn run(mut self) -> Result<(), Error> {
        loop {
            select! {
                device_id_request_stream = self.device_id_clients.select_next_some() => {
                    info!("Received FIDL client connection to `DeviceIdentification`");
                    self.device_id_requests.push(device_id_request_stream);
                }
                device_id_request = self.device_id_requests.select_next_some() => {
                    match device_id_request {
                        Ok(req) => self.handle_device_id_request(req).await,
                        Err(e) => warn!("Error receiving DI request: {:?}", e),
                    }
                }
                complete => {
                    break;
                }
            }
        }
        Ok(())
    }

    async fn handle_device_id_request(&mut self, request: di::DeviceIdentificationRequest) {
        let di::DeviceIdentificationRequest::SetDeviceIdentification { records, .. } = request;
        info!("Received SetDeviceIdentification request: {:?}", records);
    }
}
