// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod httpsdate;

use crate::httpsdate::{HttpsDateUpdateAlgorithm, RetryStrategy};
use anyhow::{Context, Error};
use fidl_fuchsia_time_external::{PushSourceRequestStream, Status};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon as zx;
use futures::{future::join, StreamExt, TryFutureExt};
use log::warn;
use push_source::PushSource;

/// Retry strategy used while polling for time.
const RETRY_STRATEGY: RetryStrategy = RetryStrategy {
    min_between_failures: zx::Duration::from_seconds(1),
    max_exponent: 3,
    tries_per_exponent: 3,
    between_successes: zx::Duration::from_minutes(30),
};

/// URI used to obtain time samples.
// TODO(fxbug.dev/56875): Decide on correct endpoint and move to config data.
const REQUEST_URI: &str = "https://clients1.google.com/generate_204";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["time"]).context("initializing logging")?;

    let update_algorithm = HttpsDateUpdateAlgorithm::new(RETRY_STRATEGY, REQUEST_URI.parse()?);
    let push_source = PushSource::new(update_algorithm, Status::Ok)?;
    let update_fut = push_source.poll_updates();

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: PushSourceRequestStream| stream);

    fs.take_and_serve_directory_handle()?;
    let service_fut = fs.for_each_concurrent(None, |stream| {
        push_source
            .handle_requests_for_stream(stream)
            .unwrap_or_else(|e| warn!("Error handling PushSource stream: {:?}", e))
    });

    let (update_res, _service) = join(update_fut, service_fut).await;
    update_res
}
