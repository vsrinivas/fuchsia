// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::{endpoints::RequestStream, handle::AsyncChannel},
    fidl_fuchsia_process_lifecycle::{LifecycleRequest, LifecycleRequestStream},
    fuchsia_async::{self as fasync},
    // fuchsia_component::server::ServiceFs,
    fuchsia_component,
    fuchsia_runtime::{self as fruntime, HandleInfo, HandleType},
    fuchsia_zircon::{self as zx},
    futures_util::stream::{StreamExt, TryStreamExt},
    std::{process, thread, time},
    tracing::{error, info},
};

/// Takes the Lifecycle handle passed by the Runner. The program listens to the
/// channel for requests, but then doesn't actually exit. This component also
/// serves an empty ServiceFs so the
/// lifecycle_timeout_unresponsive_integration_test can cause this component to
/// start and get a failure to connect to a capability that this component says
/// it provides, but actually does not.
#[fuchsia::component]
async fn main() {
    // Spawn this in its own task so that we can run the ServiceFs below.
    fasync::Task::spawn(async move {
        match fruntime::take_startup_handle(HandleInfo::new(HandleType::Lifecycle, 0)) {
            Some(lifecycle_handle) => {
                info!("Lifecycle channel received.");
                let x: zx::Channel = lifecycle_handle.into();
                let async_x = AsyncChannel::from(
                    fasync::Channel::from_channel(x).expect("Async channel conversion failed."),
                );
                let mut req_stream = LifecycleRequestStream::from_channel(async_x);
                info!("Awaiting request to close");
                while let Some(request) =
                    req_stream.try_next().await.expect("Failure receiving lifecycle FIDL message")
                {
                    match request {
                        LifecycleRequest::Stop { control_handle: _ } => {
                            info!("Received request to stop, but I refuse to go!");
                            let half_hour = time::Duration::from_secs(30 * 60);
                            loop {
                                thread::sleep(half_hour);
                            }
                        }
                    }
                }

                // We only arrive here if the lifecycle channel closed without
                // first sending the shutdown event, which is unexpected.
                process::abort();
            }
            None => {
                // We did not receive a lifecycle channel, exit abnormally.
                error!("No lifecycle channel received, exiting.");
                process::abort();
            }
        }
    })
    .detach();

    // Serve the outgoing directory so if anything tries to connect to a
    // capability we say we provide, but donn't actually provide, its channel
    // will be closed.
    let mut fs = fuchsia_component::server::ServiceFs::new_local();
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing");
    fs.collect::<()>().await;
}
