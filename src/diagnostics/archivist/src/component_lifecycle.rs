// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_lock::Mutex;
use fidl::endpoints::RequestStream;
use fidl_fuchsia_diagnostics_test::{ControllerRequest, ControllerRequestStream};
use fidl_fuchsia_process_lifecycle::{LifecycleRequest, LifecycleRequestStream};
use fuchsia_async as fasync;
use fuchsia_runtime::{take_startup_handle, HandleInfo, HandleType};
use futures::{
    channel::{mpsc, oneshot},
    StreamExt,
};
use std::sync::Arc;
use tracing::{debug, error};

/// Serves the Lifecycle protocol from the v2 component runtime used for controlled shutdown of the
/// archivist in v2.
pub fn serve_v2() -> (fasync::Task<()>, oneshot::Receiver<()>) {
    let (stop_sender, stop_recv) = oneshot::channel();
    let mut stop_sender = Some(stop_sender);

    let lifecycle_handle_info = HandleInfo::new(HandleType::Lifecycle, 0);
    let lifecycle_handle = take_startup_handle(lifecycle_handle_info)
        .expect("must have been provided a lifecycle channel in procargs");
    let async_chan = fasync::Channel::from_channel(lifecycle_handle.into())
        .expect("Async channel conversion failed.");
    let mut req_stream = LifecycleRequestStream::from_channel(async_chan);

    let task = fasync::Task::spawn(async move {
        debug!("Awaiting request to close");
        while let Some(Ok(LifecycleRequest::Stop { .. })) = req_stream.next().await {
            debug!("Initiating shutdown.");
            if let Some(sender) = stop_sender.take() {
                if sender.send(()).is_err() {
                    error!("Archivist not shutting down. We lost the stop recv end");
                }
            }
        }
    });
    (task, stop_recv)
}

pub struct TestControllerServer {
    stop_sender: Arc<Mutex<Option<oneshot::Sender<()>>>>,
    server_task_sender: mpsc::UnboundedSender<fasync::Task<()>>,
    _server_task_drainer: fasync::Task<()>,
}

impl TestControllerServer {
    pub fn new() -> (Self, oneshot::Receiver<()>) {
        let (stop_sender, stop_recv) = oneshot::channel();
        let (server_task_sender, rcv) = mpsc::unbounded();
        (
            Self {
                stop_sender: Arc::new(Mutex::new(Some(stop_sender))),
                server_task_sender,
                _server_task_drainer: fasync::Task::spawn(async move {
                    rcv.for_each_concurrent(None, |rx| async move { rx.await }).await
                }),
            },
            stop_recv,
        )
    }

    /// Serves the test Controller used for controlled shutdown of the archivist in v1 tests.
    pub fn spawn(&mut self, mut stream: ControllerRequestStream) {
        let stop_sender = self.stop_sender.clone();
        self.server_task_sender
            .unbounded_send(fasync::Task::spawn(async move {
                match stream.next().await {
                    Some(Ok(ControllerRequest::Stop { .. })) => {
                        debug!("Stop request received.");
                        if let Some(sender) = stop_sender.lock().await.take() {
                            if sender.send(()).is_err() {
                                error!("Archivist not shutting down. We lost the stop recv end");
                            }
                        }
                    }
                    Some(Err(err)) => {
                        error!(%err, "error serving controller");
                    }
                    None => {}
                }
            }))
            .ok();
    }
}
