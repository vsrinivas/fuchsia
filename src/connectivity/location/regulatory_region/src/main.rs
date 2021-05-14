// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_location_namedplace::{
        RegulatoryRegionConfiguratorRequest as ConfigRequest,
        RegulatoryRegionConfiguratorRequestStream as ConfigRequestStream,
        RegulatoryRegionWatcherGetUpdateResponder as WatchUpdateResponder,
        RegulatoryRegionWatcherRequest as WatchRequest,
        RegulatoryRegionWatcherRequestStream as WatchRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    regulatory_region_lib::pub_sub_hub::PubSubHub,
    std::path::Path,
};

const CONCURRENCY_LIMIT: Option<usize> = None;
const REGION_CODE_PATH: &str = "/cache/regulatory_region.json";

enum IncomingService {
    ConfigRequest(ConfigRequestStream),
    WatchRequest(WatchRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init().context("Failed to initialize logging")?;
    let mut fs = ServiceFs::new_local();
    let storage_path = Path::new(REGION_CODE_PATH);
    let region_tracker = PubSubHub::new(storage_path.into());
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

/// Watch for requests from either GetRegionUpdate or deprecated GetUpdate. A `None` response to
/// a GetRegionUpdate means that the region code is not set.
async fn process_watch_requests(
    region_tracker: &PubSubHub,
    mut stream: WatchRequestStream,
) -> Result<(), Error> {
    // If an update is requested with GetUpdate, the first value will be sent after the value has
    // been set. If it is a GetRegionUpdate, the first value will always be sent immediately.
    let mut last_read_value =
        if let Some(request) = stream.try_next().await.context("Failed to read Watcher request")? {
            match request {
                WatchRequest::GetUpdate { responder } => {
                    respond_to_get_update(None, region_tracker, responder).await?
                }
                WatchRequest::GetRegionUpdate { responder } => {
                    let val = region_tracker.get_value();
                    responder
                        .send(val.as_ref().map(|s| s.as_ref()))
                        .context("Failed to write response")?;
                    val
                }
            }
        } else {
            return Ok(());
        };

    while let Some(request) = stream.try_next().await.context("Failed to read Watcher request")? {
        match request {
            WatchRequest::GetUpdate { responder } => {
                last_read_value =
                    respond_to_get_update(last_read_value, region_tracker, responder).await?;
            }
            WatchRequest::GetRegionUpdate { responder } => {
                let val = region_tracker.watch_for_change(last_read_value).await;
                responder
                    .send(val.as_ref().map(|s| s.as_ref()))
                    .context("Failed to write response")?;
                last_read_value = val;
            }
        }
    }
    Ok(())
}

/// Wait for an update and handle responding to the GetUpdate request. Returns the new value.
async fn respond_to_get_update(
    last_read_value: Option<String>,
    region_tracker: &PubSubHub,
    responder: WatchUpdateResponder,
) -> Result<Option<String>, Error> {
    match region_tracker.watch_for_change(last_read_value).await {
        Some(val) => {
            responder.send(val.as_ref()).context("Failed to write response")?;
            Ok(Some(val))
        }
        None => panic!("Internal error: new value is None"),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_location_namedplace::RegulatoryRegionWatcherMarker,
        fuchsia_async as fasync, matches::assert_matches, pin_utils::pin_mut, std::task::Poll,
        tempfile::TempDir,
    };

    #[test]
    fn process_watch_requests_sends_first_none() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create executor");
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (client, requests) = fidl::endpoints::create_proxy::<RegulatoryRegionWatcherMarker>()
            .expect("Failed to connect to Watcher protocol");
        let update_stream = requests.into_stream().expect("Failed to create stream");

        let watch_fut = process_watch_requests(&hub, update_stream);
        pin_mut!(watch_fut);

        // Request an update.
        let get_update_fut = client.get_region_update();
        pin_mut!(get_update_fut);

        // After running process_watch_requests the initial PubSubHub value should be sent.
        assert!(exec.run_until_stalled(&mut watch_fut).is_pending());
        assert_matches!(exec.run_until_stalled(&mut get_update_fut), Poll::Ready(Ok(None)));

        // Subsequent update requests should resolve after there is a changed value.
        let get_update_fut = client.get_region_update();
        pin_mut!(get_update_fut);
        assert!(exec.run_until_stalled(&mut watch_fut).is_pending());
        assert!(exec.run_until_stalled(&mut get_update_fut).is_pending());

        // Change the internal value and check that we get an update.
        hub.publish("US");
        assert!(exec.run_until_stalled(&mut watch_fut).is_pending());

        assert_matches!(
            exec.run_until_stalled(&mut get_update_fut),
            Poll::Ready(Ok(Some(region))) if region.as_str() == "US"
        );
    }

    #[test]
    fn first_update_is_current_value() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create executor");
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (client, requests) = fidl::endpoints::create_proxy::<RegulatoryRegionWatcherMarker>()
            .expect("Failed to connect to Watcher protocol");
        let update_stream = requests.into_stream().expect("Failed to create stream");

        // Start processing update requests.
        let watch_fut = process_watch_requests(&hub, update_stream);
        pin_mut!(watch_fut);

        // Change the internal value before requesting first update.
        hub.publish("US");

        // The first update should be the current value, not the initial value.
        let get_update_fut = client.get_region_update();
        pin_mut!(get_update_fut);

        assert!(exec.run_until_stalled(&mut watch_fut).is_pending());
        assert_matches!(
            exec.run_until_stalled(&mut get_update_fut),
            Poll::Ready(Ok(Some(region))) if region.as_str() == "US"
        );
    }
}
