// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::watcher,
    anyhow::Error,
    fidl::endpoints::RequestStream,
    fidl_fuchsia_fshost as fshost,
    fidl_fuchsia_process_lifecycle::{LifecycleRequest, LifecycleRequestStream},
    fuchsia_async as fasync,
    fuchsia_runtime::HandleType,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, StreamExt, TryStreamExt},
    std::sync::Arc,
    vfs::service,
};

pub enum FshostShutdownResponder {
    Lifecycle(LifecycleRequestStream),
}

impl FshostShutdownResponder {
    pub fn close(self) -> Result<(), fidl::Error> {
        match self {
            FshostShutdownResponder::Lifecycle(_) => {}
        }
        Ok(())
    }
}

/// Make a new vfs service node that implements fuchsia.fshost.Admin
pub fn fshost_admin() -> Arc<service::Service> {
    service::host(move |mut stream: fshost::AdminRequestStream| async move {
        while let Some(request) = stream.next().await {
            match request {
                Ok(fshost::AdminRequest::Mount { responder, .. }) => {
                    responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw())).unwrap_or_else(
                        |e| {
                            tracing::error!("failed to send Mount response. error: {:?}", e);
                        },
                    );
                }
                Ok(fshost::AdminRequest::Unmount { responder, .. }) => {
                    responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw())).unwrap_or_else(
                        |e| {
                            tracing::error!("failed to send Unmount response. error: {:?}", e);
                        },
                    );
                }
                Ok(fshost::AdminRequest::GetDevicePath { responder, .. }) => {
                    responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw())).unwrap_or_else(
                        |e| {
                            tracing::error!(
                                "failed to send GetDevicePath response. error: {:?}",
                                e
                            );
                        },
                    );
                }
                Ok(fshost::AdminRequest::WriteDataFile { responder, .. }) => {
                    responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw())).unwrap_or_else(
                        |e| {
                            tracing::error!(
                                "failed to send WriteDataFile response. error: {:?}",
                                e
                            );
                        },
                    );
                }
                Err(e) => {
                    tracing::error!("admin server failed: {:?}", e);
                    return;
                }
            }
        }
    })
}

/// Create a new service node which implements the fuchsia.fshost.BlockWatcher protocol.
pub fn fshost_block_watcher(pauser: watcher::Watcher) -> Arc<service::Service> {
    service::host(move |mut stream: fshost::BlockWatcherRequestStream| {
        let mut pauser = pauser.clone();
        async move {
            while let Some(request) = stream.next().await {
                match request {
                    Ok(fshost::BlockWatcherRequest::Pause { responder }) => {
                        let res = match pauser.pause().await {
                            Ok(()) => zx::Status::OK.into_raw(),
                            Err(e) => {
                                tracing::error!("block watcher service: failed to pause: {:?}", e);
                                zx::Status::UNAVAILABLE.into_raw()
                            }
                        };
                        responder.send(res).unwrap_or_else(|e| {
                            tracing::error!("failed to send Pause response. error: {:?}", e);
                        });
                    }
                    Ok(fshost::BlockWatcherRequest::Resume { responder }) => {
                        let res = match pauser.resume().await {
                            Ok(()) => zx::Status::OK.into_raw(),
                            Err(e) => {
                                tracing::error!("block watcher service: failed to resume: {:?}", e);
                                zx::Status::BAD_STATE.into_raw()
                            }
                        };
                        responder.send(res).unwrap_or_else(|e| {
                            tracing::error!("failed to send Resume response. error: {:?}", e);
                        });
                    }
                    Err(e) => {
                        tracing::error!("block watcher server failed: {:?}", e);
                        return;
                    }
                }
            }
        }
    })
}

pub fn handle_lifecycle_requests(
    mut shutdown: mpsc::Sender<FshostShutdownResponder>,
) -> Result<(), Error> {
    if let Some(handle) = fuchsia_runtime::take_startup_handle(HandleType::Lifecycle.into()) {
        let mut stream =
            LifecycleRequestStream::from_channel(fasync::Channel::from_channel(handle.into())?);
        fasync::Task::spawn(async move {
            if let Ok(Some(LifecycleRequest::Stop { .. })) = stream.try_next().await {
                shutdown.start_send(FshostShutdownResponder::Lifecycle(stream)).unwrap_or_else(
                    |e| tracing::error!("failed to send shutdown message. error: {:?}", e),
                );
            }
        })
        .detach();
    }
    Ok(())
}
