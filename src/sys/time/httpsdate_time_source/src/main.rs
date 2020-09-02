// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod push_source;

use crate::push_source::{PushSource, Update, UpdateAlgorithm};
use anyhow::{Context, Error};
use async_trait::async_trait;
use fidl_fuchsia_time_external::{Properties, PushSourceRequestStream, Status};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::{channel::mpsc::Sender, future::join, StreamExt, TryFutureExt};
use log::warn;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["time"]).context("initializing logging")?;

    let push_source = PushSource::new(DummyAlgorithm, Status::Ok);
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

/// Temporary definition of an UpdateAlgorithm. Will be replaced with a source that makes HTTPS
/// calls.
struct DummyAlgorithm;

#[async_trait]
impl UpdateAlgorithm for DummyAlgorithm {
    fn update_device_properties(&self, _properties: Properties) {
        // do nothing
    }

    async fn generate_updates(&self, _sink: Sender<Update>) -> Result<(), Error> {
        // do nothing and hang
        futures::future::pending::<()>().await;
        Ok(())
    }
}
