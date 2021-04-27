// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::Error;
use fidl_fuchsia_fakeclock_test::{ExampleRequest, ExampleRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon as zx;
use futures::{StreamExt, TryStreamExt};
use log::warn;
use named_timer::DeadlineId;

const DEADLINE_NAME: DeadlineId<'static> = DeadlineId::new("fake-clock-example", "deadline");

#[fasync::run_singlethreaded]
async fn main() {
    let () = fuchsia_syslog::init().expect("failed to initialize logger");
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: ExampleRequestStream| stream);
    fs.take_and_serve_directory_handle().expect("failed to serve directory handle");
    fs.for_each_concurrent(None, |stream| async move {
        let () = handle_requests_for_stream(stream)
            .await
            .unwrap_or_else(|e| warn!("Error serving request: {:?}", e));
    })
    .await
}

async fn handle_requests_for_stream(stream: ExampleRequestStream) -> Result<(), Error> {
    stream
        .try_for_each_concurrent(None, |req| async move {
            match req {
                ExampleRequest::GetMonotonic { responder } => {
                    responder.send(zx::Time::get_monotonic().into_nanos())
                }
                ExampleRequest::WaitUntil { timeout, responder } => {
                    let () =
                        fasync::Timer::new(fasync::Time::from_zx(zx::Time::from_nanos(timeout)))
                            .await;
                    responder.send()
                }
                ExampleRequest::WaitFor { duration, responder } => {
                    let () = named_timer::NamedTimer::new(
                        &DEADLINE_NAME,
                        zx::Duration::from_nanos(duration),
                    )
                    .await;
                    responder.send()
                }
            }
        })
        .await
}
