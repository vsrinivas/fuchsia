// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::{endpoints::RequestStream, AsyncChannel};
use fidl_fuchsia_diagnostics_test::{ControllerRequest, ControllerRequestStream};
use fidl_fuchsia_process_lifecycle::{LifecycleRequest, LifecycleRequestStream};
use fuchsia_async as fasync;
use fuchsia_runtime::{take_startup_handle, HandleInfo, HandleType};
use futures::{channel::mpsc, SinkExt, TryStreamExt};
use tracing::{debug, error, info};

/// Serves the test Controller used for controlled shutdown of the archivist in v1 tests.
pub async fn serve_test_controller(
    mut stream: ControllerRequestStream,
    mut stop_sender: mpsc::Sender<()>,
) {
    match stream.try_next().await {
        Ok(Some(ControllerRequest::Stop { .. })) => {
            debug!("Stop request received.");
            stop_sender.send(()).await.ok();
        }
        Ok(None) => {}
        Err(e) => {
            error!(%e, "error serving controller");
        }
    }
}

/// Serves the Lifecycle protocol from the v2 component runtime used for controlled shutdown of the
/// archivist in v2.
pub fn serve_v2() -> (fasync::Task<()>, mpsc::Receiver<()>) {
    let (mut stop_sender, stop_recv) = mpsc::channel(0);

    let lifecycle_handle_info = HandleInfo::new(HandleType::Lifecycle, 0);
    let lifecycle_handle = take_startup_handle(lifecycle_handle_info)
        .expect("must have been provided a lifecycle channel in procargs");
    let async_chan = AsyncChannel::from(
        fasync::Channel::from_channel(lifecycle_handle.into())
            .expect("Async channel conversion failed."),
    );
    let mut req_stream = LifecycleRequestStream::from_channel(async_chan);

    let task = fasync::Task::spawn(async move {
        debug!("Awaiting request to close");
        while let Some(LifecycleRequest::Stop { .. }) =
            req_stream.try_next().await.expect("Failure receiving lifecycle FIDL message")
        {
            info!("Initiating shutdown.");
            stop_sender.send(()).await.unwrap();
        }
    });
    (task, stop_recv)
}
