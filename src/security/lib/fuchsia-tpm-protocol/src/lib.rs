// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod command;
mod deprovision;
mod provision;

use crate::{
    command::handle_command_request, deprovision::handle_deprovision_request,
    provision::handle_provision_request,
};
use anyhow::Error;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_tpm::{
    CommandRequestStream, DeprovisionRequestStream, ProvisionRequestStream, TpmDeviceMarker,
    TpmDeviceProxy,
};
use futures::{lock::Mutex, prelude::*};
use std::sync::Arc;
use tracing;

/// Implements a FIDL service for the available protocols defined in the
/// `fuchsia.tpm` namespace.
///
/// The implementation guarantees only a single consumer can access the
/// underlying `TpmDeviceProxy` at a time. This implements a compliant TCG TAB
/// interface, however it is not a fair scheduled TAB. It is a simple FIFO
/// queue. TCG Resource Management is NOT currently implemented and it is the
/// consumer's responsibility to call ContextSave/ContextLoad on each use to
/// the TPM.
pub struct TpmProtocol {
    // The `device_proxy` must be protected from concurrent access to prevent
    // commands trampling each other in the driver.
    // NOTE: we have multiple driver implementations for different board types
    // and we cannot make assumptions about the queue size or order of
    // operations.
    device_proxy: Mutex<TpmDeviceProxy>,
}

impl TpmProtocol {
    /// Constructs a new `TpmProtocol` instance wrapping the provided
    /// `device_client_end` in a mutex.
    pub fn new(device_client_end: ClientEnd<TpmDeviceMarker>) -> Result<Arc<Self>, Error> {
        Ok(Arc::new(Self { device_proxy: Mutex::new(device_client_end.into_proxy()?) }))
    }

    /// Handles a stream of command requests in the order received one at a
    /// time. This function will await until the `device_proxy` is available.
    pub async fn handle_command_stream(
        &self,
        mut request_stream: CommandRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = request_stream.try_next().await? {
            let device = self.device_proxy.lock().await;
            handle_command_request(request, device)
                .unwrap_or_else(|e| {
                    tracing::error!("Error handling FIDL request: {:?}", e);
                })
                .await
        }
        Ok(())
    }

    /// Handles a stream of provision requests in the order received one at a
    /// time. This function will await until the `device_proxy` is available.
    pub async fn handle_provision_stream(
        &self,
        mut request_stream: ProvisionRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = request_stream.try_next().await? {
            let device = self.device_proxy.lock().await;
            handle_provision_request(request, device)
                .unwrap_or_else(|e| {
                    tracing::error!("Error handling FIDL request: {:?}", e);
                })
                .await
        }
        Ok(())
    }

    /// Handles a stream of deprovision requests in the order received one at
    /// a time. This function will await until the `device_proxy` is available.
    pub async fn handle_deprovision_stream(
        &self,
        mut request_stream: DeprovisionRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = request_stream.try_next().await? {
            let device = self.device_proxy.lock().await;
            handle_deprovision_request(request, device)
                .unwrap_or_else(|e| {
                    tracing::error!("Error handling FIDL request: {:?}", e);
                })
                .await
        }
        Ok(())
    }
}
