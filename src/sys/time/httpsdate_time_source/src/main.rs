// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod bound;
mod constants;
mod datatypes;
mod diagnostics;
mod httpsdate;
mod sampler;

use crate::diagnostics::{CobaltDiagnostics, CompositeDiagnostics, InspectDiagnostics};
use crate::httpsdate::{HttpsDateUpdateAlgorithm, RetryStrategy};
use crate::sampler::HttpsSamplerImpl;
use anyhow::{Context, Error};
use fidl_fuchsia_net_interfaces::StateMarker;
use fidl_fuchsia_time_external::{PushSourceRequestStream, Status};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon as zx;
use futures::{future::join3, FutureExt, StreamExt, TryFutureExt};
use log::warn;
use push_source::PushSource;

/// Retry strategy used while polling for time.
const RETRY_STRATEGY: RetryStrategy = RetryStrategy {
    min_between_failures: zx::Duration::from_seconds(1),
    max_exponent: 3,
    tries_per_exponent: 3,
    converge_time_between_samples: zx::Duration::from_minutes(2),
    maintain_time_between_samples: zx::Duration::from_minutes(20),
};

/// URI used to obtain time samples.
// TODO(fxbug.dev/68621): Allow configuration per product.
const REQUEST_URI: &str = "https://clients3.google.com/generate_204";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["time"]).context("initializing logging")?;

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: PushSourceRequestStream| stream);

    let inspect = InspectDiagnostics::new(fuchsia_inspect::component::inspector().root());
    let (cobalt, cobalt_sender_fut) = CobaltDiagnostics::new();
    let diagnostics = CompositeDiagnostics::new(inspect, cobalt);

    inspect_runtime::serve(fuchsia_inspect::component::inspector(), &mut fs)?;

    let sampler = HttpsSamplerImpl::new(REQUEST_URI.parse()?);

    let interface_state_service = fuchsia_component::client::connect_to_protocol::<StateMarker>()
        .context("failed to connect to fuchsia.net.interfaces/State")?;
    let internet_reachable = fidl_fuchsia_net_interfaces_ext::wait_for_reachability(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state_service)
            .context("failed to create network interface event stream")?,
    )
    .map(|r| r.context("reachability status stream error"));

    let update_algorithm =
        HttpsDateUpdateAlgorithm::new(RETRY_STRATEGY, diagnostics, sampler, internet_reachable);
    let push_source = PushSource::new(update_algorithm, Status::Initializing)?;
    let update_fut = push_source.poll_updates();

    fs.take_and_serve_directory_handle()?;
    let service_fut = fs.for_each_concurrent(None, |stream| {
        push_source
            .handle_requests_for_stream(stream)
            .unwrap_or_else(|e| warn!("Error handling PushSource stream: {:?}", e))
    });

    let (update_res, _, _) = join3(update_fut, service_fut, cobalt_sender_fut).await;
    update_res
}
