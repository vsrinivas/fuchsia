// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_fshost as fshost, fuchsia_zircon as zx,
    futures::{channel::mpsc, StreamExt},
    std::sync::Arc,
    vfs::service,
};

pub enum FshostShutdownResponder {
    Admin(fshost::AdminShutdownResponder),
}

impl FshostShutdownResponder {
    pub fn close(self) -> Result<(), fidl::Error> {
        match self {
            FshostShutdownResponder::Admin(responder) => responder.send()?,
        }
        Ok(())
    }
}

/// Make a new vfs service node that implements fuchsia.fshost.Admin
pub fn fshost_admin(shutdown_tx: mpsc::Sender<FshostShutdownResponder>) -> Arc<service::Service> {
    service::host(move |mut stream: fshost::AdminRequestStream| {
        let mut shutdown_tx = shutdown_tx.clone();
        async move {
            while let Some(request) = stream.next().await {
                match request {
                    Ok(fshost::AdminRequest::Shutdown { responder }) => {
                        shutdown_tx
                            .start_send(FshostShutdownResponder::Admin(responder))
                            .unwrap_or_else(|e| {
                                log::error!("failed to send shutdown message. error: {:?}", e);
                            });
                        return;
                    }
                    Ok(fshost::AdminRequest::Mount { responder, .. }) => {
                        responder
                            .send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))
                            .unwrap_or_else(|e| {
                                log::error!("failed to send Mount response. error: {:?}", e);
                            });
                    }
                    Ok(fshost::AdminRequest::Unmount { responder, .. }) => {
                        responder
                            .send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))
                            .unwrap_or_else(|e| {
                                log::error!("failed to send Unmount response. error: {:?}", e);
                            });
                    }
                    Ok(fshost::AdminRequest::GetDevicePath { responder, .. }) => {
                        responder
                            .send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))
                            .unwrap_or_else(|e| {
                                log::error!(
                                    "failed to send GetDevicePath response. error: {:?}",
                                    e
                                );
                            });
                    }
                    Err(e) => {
                        log::error!("admin server failed: {:?}", e);
                        return;
                    }
                }
            }
            log::error!("admin server exited unexpectedly");
        }
    })
}
