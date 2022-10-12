// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod discovery;

use crate::discovery::find_tpm20;
use anyhow::{anyhow, Context};
use fidl::endpoints::{ClientEnd, Proxy};
use fidl_fuchsia_tpm::{
    CommandRequestStream, DeprovisionRequestStream, ProvisionRequestStream, TpmDeviceMarker,
};
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::{component, health::Reporter};
use fuchsia_tpm_protocol::TpmProtocol;
use futures::prelude::*;
use std::sync::Arc;
use tracing;

/// Wraps all hosted protocols into a single type for simple match dispatching.
enum IncomingRequest {
    /// Command is a general purpose API for communicating with the TPM and is used by
    /// any component attempting to implement hardware backed cryptography. It guarantees
    /// only a single user will be able to access the TPM at any given time.
    Command(CommandRequestStream),
    /// Provisioning is a restricted API that deals with taking ownership of the TPM.
    /// This occurs once per operating system install.
    Provision(ProvisionRequestStream),
    /// Deprovisioning is a restricted API that deals with removing ownership of the TPM.
    /// This occurs once during factory reset.
    Deprovision(DeprovisionRequestStream),
}

#[fuchsia::main(logging = true)]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();
    inspect_runtime::serve(component::inspector(), &mut service_fs)?;
    component::health().set_starting_up();
    tracing::debug!("Finding TPM device");

    // Terminate the component if we are unable to find a TPM20.
    let tpm_device_proxy = match find_tpm20().await {
        Ok(proxy) => proxy,
        Err(e) => {
            tracing::warn!("Unable to find tpm20: {:?}", e);
            component::health().set_unhealthy("No TPM20 found");
            return Ok(());
        }
    };

    service_fs
        .dir("svc")
        .add_fidl_service(IncomingRequest::Command)
        .add_fidl_service(IncomingRequest::Provision)
        .add_fidl_service(IncomingRequest::Deprovision);
    service_fs.take_and_serve_directory_handle().context("Failed to serve outgoing namespace")?;

    component::health().set_ok();
    tracing::debug!("Initialized");

    let tpm = TpmProtocol::new(ClientEnd::<TpmDeviceMarker>::new(
        tpm_device_proxy.into_channel().map_err(|e| anyhow!("{:?}", e))?.into_zx_channel(),
    ))?;
    let tpm_ref = &tpm;
    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async move {
            match request {
                IncomingRequest::Command(stream) => {
                    Arc::clone(tpm_ref).handle_command_stream(stream).await.unwrap_or_else(|e| {
                        tracing::error!("Failed while handling transmit requests: {:?}", e);
                    });
                }
                IncomingRequest::Provision(stream) => {
                    Arc::clone(tpm_ref).handle_provision_stream(stream).await.unwrap_or_else(|e| {
                        tracing::error!("Failed while handling provision requests: {:?}", e);
                    });
                }
                IncomingRequest::Deprovision(stream) => {
                    Arc::clone(tpm_ref).handle_deprovision_stream(stream).await.unwrap_or_else(
                        |e| {
                            tracing::error!("Failed while handling deprovision requests: {:?}", e);
                        },
                    );
                }
            }
        })
        .await;

    Ok(())
}
