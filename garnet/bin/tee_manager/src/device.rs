// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_hardware_tee::DeviceConnectorProxy,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    fuchsia_zircon::AsHandleRef,
    futures::{future::AbortHandle, lock::Mutex, prelude::*},
    std::{fs::File, path::Path, sync::Arc},
};

struct AbortContext {
    pub is_aborted: bool,
    pub abort_handles: Vec<AbortHandle>,
}

impl AbortContext {
    pub fn new() -> Self {
        Self { is_aborted: false, abort_handles: Vec::new() }
    }
}

/// `TeeDeviceConnection` represents a connection to a TEE device driver serving
/// fuchsia.hardware.tee.DeviceConnector.
#[derive(Clone, Debug)]
pub struct TeeDeviceConnection {
    pub connector_proxy: DeviceConnectorProxy,
    abort_handles_lock: Arc<Mutex<AbortContext>>,
}

impl TeeDeviceConnection {
    pub fn create(path: impl AsRef<Path>) -> Result<Self, Error> {
        let file = File::open(path).context("Failed to open TEE device in devfs")?;
        let devfs_zx_chan = zx::Channel::from(
            fdio::transfer_fd(file).context("Could not convert devfs file descriptor to handle")?,
        );

        // Create a Rust async channel from the zx::Channel
        let devfs_chan = fasync::Channel::from_channel(devfs_zx_chan)?;
        // Convert the channel to a FIDL connection for the ManagerConnector
        let connector_proxy = DeviceConnectorProxy::new(devfs_chan);

        // Start a future that invokes registered AbortHandles upon device connection closing
        let abort_handles_lock = Arc::new(Mutex::new(AbortContext::new()));
        let abort_handles_lock_cloned = abort_handles_lock.clone();
        let on_closed_fut = fasync::OnSignals::new(
            &connector_proxy.as_handle_ref(),
            zx::Signals::CHANNEL_PEER_CLOSED,
        )
        // TODO(godtamit): `on_closed_fut` may outlive the connector_proxy,
        // and won't fire ever if all references to `connector_proxy` are dropped.
        // Change this instead to abort the async task below if something goes wrong,
        // causing the connection to be aborted.
        .extend_lifetime()
        .map(|res| res.map(|_| ()))
        .unwrap_or_else(|e| fx_log_err!("{:?}", e));

        fasync::spawn(async move {
            let abort_handles_lock = abort_handles_lock_cloned;
            on_closed_fut.await;

            let abort_context = &mut *abort_handles_lock.lock().await;
            abort_context.is_aborted = true;
            for handle in &abort_context.abort_handles {
                handle.abort();
            }
            abort_context.abort_handles.clear();
        });

        Ok(Self { connector_proxy, abort_handles_lock })
    }

    /// Register an `AbortHandle` to be invoked when the device connection closes.
    pub async fn register_abort_handle_on_closed(&self, handle: AbortHandle) {
        let abort_context = &mut *self.abort_handles_lock.lock().await;
        if abort_context.is_aborted {
            // Since the device closed event has already been received, immediately close the handle
            handle.abort();
        }

        abort_context.abort_handles.push(handle);
    }
}
