// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_location_namedplace::{
        RegulatoryRegionConfiguratorRequest as ConfigRequest,
        RegulatoryRegionConfiguratorRequestStream as ConfigRequestStream,
        RegulatoryRegionWatcherRequest as WatchRequest,
        RegulatoryRegionWatcherRequestStream as WatchRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    regulatory_region_lib::pub_sub_hub::PubSubHub,
};

const CONCURRENCY_LIMIT: Option<usize> = None;

enum IncomingService {
    ConfigRequest(ConfigRequestStream),
    WatchRequest(WatchRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init().context("Failed to initialize logging")?;
    let mut fs = ServiceFs::new_local();
    let region_tracker = PubSubHub::new();
    fs.dir("svc").add_fidl_service(IncomingService::ConfigRequest);
    fs.dir("svc").add_fidl_service(IncomingService::WatchRequest);
    fs.take_and_serve_directory_handle().context("Failed to start serving")?;
    fs.for_each_concurrent(CONCURRENCY_LIMIT, |client| {
        handle_incoming_service(&region_tracker, client)
            .unwrap_or_else(|e| fx_log_info!("Connection terminated: {:?}", e))
    })
    .await;
    Ok(())
}

async fn handle_incoming_service(
    region_tracker: &PubSubHub,
    protocol: IncomingService,
) -> Result<(), Error> {
    match protocol {
        IncomingService::ConfigRequest(client) => {
            process_config_requests(region_tracker, client).await
        }
        IncomingService::WatchRequest(client) => {
            process_watch_requests(region_tracker, client).await
        }
    }
}

async fn process_config_requests(
    region_tracker: &PubSubHub,
    mut stream: ConfigRequestStream,
) -> Result<(), Error> {
    while let Some(ConfigRequest::SetRegion { region, control_handle: _ }) =
        stream.try_next().await.context("Failed to read Configurator request")?
    {
        region_tracker.publish(region);
    }
    Ok(())
}

async fn process_watch_requests(
    region_tracker: &PubSubHub,
    mut stream: WatchRequestStream,
) -> Result<(), Error> {
    let mut last_read_value = None;
    while let Some(WatchRequest::GetUpdate { responder }) =
        stream.try_next().await.context("Failed to read Watcher request")?
    {
        match region_tracker.watch_for_change(last_read_value).await {
            Some(v) => {
                responder.send(v.as_ref()).context("Failed to write response")?;
                last_read_value = Some(v);
            }
            None => panic!("Internal error: new value is None"),
        }
    }
    Ok(())
}
