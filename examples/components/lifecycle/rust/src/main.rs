// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::{handle::AsyncChannel, prelude::*},
    fuchsia_async::{self as fasync},
    fuchsia_runtime::{self as fruntime, HandleInfo, HandleType},
    fuchsia_zircon::{self as zx},
    futures_util::stream::TryStreamExt,
    std::process,
    tracing::{error, info},
};

// [START imports]
use fidl_fuchsia_process_lifecycle::{LifecycleRequest, LifecycleRequestStream};
// [END imports]

// [START lifecycle_handler]
#[fuchsia::component]
async fn main() {
    // Take the lifecycle handle provided by the runner
    match fruntime::take_startup_handle(HandleInfo::new(HandleType::Lifecycle, 0)) {
        Some(lifecycle_handle) => {
            info!("Lifecycle channel received.");
            // Begin listening for lifecycle requests on this channel
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
                    LifecycleRequest::Stop { control_handle: c } => {
                        info!("Received request to stop, bye bye!");
                        c.shutdown();
                        process::exit(0);
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
}
// [END lifecycle_handler]
