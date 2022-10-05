// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod cr50;
mod power_button;
mod util;

use crate::{cr50::Cr50, power_button::PowerButton};
use anyhow::{anyhow, Context, Error};
use fidl::endpoints::Proxy;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_tpm::TpmDeviceProxy;
use fidl_fuchsia_tpm_cr50::{Cr50RequestStream, PinWeaverRequestStream};
use fuchsia_async::TimeoutExt;
use fuchsia_component::server::ServiceFs;
use fuchsia_fs::OpenFlags;
use fuchsia_inspect::{component, health::Reporter};
use fuchsia_zircon as zx;
use futures::{prelude::*, stream::TryStreamExt};
use std::{path::Path, sync::Arc};
use tracing::{debug, info, warn};

/// Wraps all hosted protocols into a single type that can be matched against
/// and dispatched.
enum IncomingRequest {
    Cr50(Cr50RequestStream),
    Pinweaver(PinWeaverRequestStream),
}

const CR50_VENDOR_ID: u16 = 0x1ae0;
const CR50_DEVICE_ID: u16 = 0x0028;

async fn is_cr50(dir: &fio::DirectoryProxy, name: &str) -> Result<Option<TpmDeviceProxy>, Error> {
    let node = fuchsia_fs::open_node(
        dir,
        Path::new(name),
        OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
        0,
    )
    .context("Sending open")?;
    let proxy = TpmDeviceProxy::new(node.into_channel().unwrap());

    let (vendor_id, device_id, _revision_id) = proxy
        .get_device_id()
        .await
        .context("Sending get device ID request")?
        .map_err(zx::Status::from_raw)
        .context("Getting device ID")?;

    if vendor_id == CR50_VENDOR_ID && device_id == CR50_DEVICE_ID {
        return Ok(Some(proxy));
    }

    info!(
        vendor_id = %format!("{:x}", vendor_id),
        device_id = %format!("{:x}", device_id),
        "Ignoring TPM with incorrect");
    Ok(None)
}

async fn find_cr50() -> Result<TpmDeviceProxy, Error> {
    let tpm_path = "/dev/class/tpm";
    let proxy = fuchsia_fs::directory::open_in_namespace(
        tpm_path,
        OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
    )
    .context("Opening TPM directory")?;

    let mut stream = Box::pin(
        device_watcher::watch_for_files(&proxy).await.context("Starting watch for TPM devices")?,
    );

    while let Some(entry) = stream
        .try_next()
        .on_timeout(fuchsia_zircon::Duration::from_seconds(300), || Ok(None))
        .await
        .context("Watching for TPM devices")?
    {
        match is_cr50(&proxy, entry.to_str().ok_or(anyhow!("Invalid path"))?).await {
            Ok(Some(proxy)) => return Ok(proxy),
            Ok(None) => {}
            Err(e) => {
                warn!("Failed to check if {}/{} is a cr50: {:?}", tpm_path, entry.display(), e)
            }
        }
    }

    return Err(anyhow!("No TPM with correct identification found!"));
}

#[fuchsia::main(logging = true)]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();

    // Initialize inspect
    inspect_runtime::serve(component::inspector(), &mut service_fs)?;
    component::health().set_starting_up();

    let proxy = match find_cr50().await {
        Ok(proxy) => proxy,
        Err(e) => {
            warn!("Could not find a cr50: {:?}", e);
            component::health().set_unhealthy("no cr50 found");
            return Ok(());
        }
    };
    let power_button = match PowerButton::new_from_namespace() {
        Ok(btn) => Some(btn),
        Err(e) => {
            warn!("Could not connect to power button monitor: {:?}", e);
            None
        }
    };
    let cr50 = Cr50::new(proxy, power_button);
    service_fs
        .dir("svc")
        .add_fidl_service(IncomingRequest::Cr50)
        .add_fidl_service(IncomingRequest::Pinweaver);

    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    component::health().set_ok();
    debug!("Initialized.");

    let cr50_ref = &cr50;
    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async move {
            match request {
                IncomingRequest::Cr50(stream) => {
                    Arc::clone(cr50_ref).handle_cr50_stream(stream).await.unwrap_or_else(|e| {
                        warn!("Failed while handling cr50 requests: {:?}", e);
                    })
                }
                IncomingRequest::Pinweaver(stream) => {
                    Arc::clone(cr50_ref).handle_pinweaver_stream(stream).await.unwrap_or_else(|e| {
                        warn!("Failed while handling pinweaver requests: {:?}", e);
                    })
                }
            }
        })
        .await;

    Ok(())
}
