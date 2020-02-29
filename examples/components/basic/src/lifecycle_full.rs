// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::{endpoints::RequestStream, handle::AsyncChannel},
    fidl_fuchsia_process_lifecycle::{LifecycleRequest, LifecycleRequestStream},
    fuchsia_async::{self as fasync},
    fuchsia_runtime::{self as fruntime, HandleInfo, HandleType},
    fuchsia_syslog::{self as fxlog, fx_log_err, fx_log_info},
    fuchsia_zircon::{self as zx},
    futures_util::stream::TryStreamExt,
    std::process,
};

/// Example which takes the Lifecycle handle passed by the Runner. The program
/// waits for a request on the channel to stop, then closes the channel and
/// exits normally (vs abnormally).
#[fasync::run_singlethreaded]
async fn main() {
    fxlog::init().expect("logger failed to start");
    match fruntime::take_startup_handle(HandleInfo::new(HandleType::Lifecycle, 0)) {
        Some(lifecycle_handle) => {
            fx_log_info!("Lifecycle channel received.");
            // We could start waiting for a message on this channel which
            // would tell us to stop. Instead we close it, indicating to our
            // Runner that we are done.
            let x: zx::Channel = lifecycle_handle.into();
            let async_x = AsyncChannel::from(
                fasync::Channel::from_channel(x).expect("Async channel conversion failed."),
            );
            let mut req_stream = LifecycleRequestStream::from_channel(async_x);
            fx_log_info!("Awaiting request to close");
            while let Some(request) =
                req_stream.try_next().await.expect("Failure receiving lifecycle FIDL message")
            {
                match request {
                    LifecycleRequest::Stop { control_handle: c } => {
                        fx_log_info!("Receive request to stop, bye bye!");
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
            fx_log_err!("No lifecycle channel received, exiting.");
            process::abort();
        }
    }
}
