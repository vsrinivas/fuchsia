// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Context, Error},
    emergency_lib::{
        bss_cache::{Bss, BssCache, BssId, RealBssCache, UpdateError},
        bss_resolver::{BssResolver, RealBssResolver, ResolverError},
    },
    emergency_metrics_registry::{
        self as metrics, EmergencyGetCurrentFailureMetricDimensionCause as GetCurrentFailure,
        EmergencyGetCurrentResultMetricDimensionResult as GetCurrentResult,
        WlanSensorReportMetricDimensionResult as WlanSensorReportResult,
        EMERGENCY_GET_CURRENT_ACCURACY_METRIC_ID as GET_CURRENT_ACCURACY_METRIC_ID,
        EMERGENCY_GET_CURRENT_FAILURE_METRIC_ID as GET_CURRENT_FAILURE_METRIC_ID,
        EMERGENCY_GET_CURRENT_LATENCY_METRIC_ID as GET_CURRENT_LATENCY_METRIC_ID,
        EMERGENCY_GET_CURRENT_RESULT_METRIC_ID as GET_CURRENT_RESULT_METRIC_ID,
        WLAN_SENSOR_REPORT_METRIC_ID,
    },
    fidl_fuchsia_location::Error as LocationError,
    fidl_fuchsia_location_position::{
        EmergencyProviderRequest, EmergencyProviderRequestStream, Position,
    },
    fidl_fuchsia_location_sensor::{
        WlanBaseStationWatcherRequest, WlanBaseStationWatcherRequestStream,
    },
    fidl_fuchsia_net_http::LoaderMarker as HttpLoaderMarker,
    fuchsia_async as fasync,
    fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType},
    fuchsia_component::{client::connect_to_service, server::ServiceFs},
    fuchsia_syslog::{self as syslog},
    futures::{lock::Mutex, prelude::*},
    log::info,
    std::{
        convert::TryFrom,
        time::{Duration, Instant},
    },
};

const CONCURRENCY_LIMIT: Option<usize> = None;
const API_KEY_FILE: &str = "/config/data/google_maps_api_key.txt";

/// Wraps all hosted protocols into a single type that can be matched against
/// and dispatched.
enum IncomingRequest {
    EmergencyProviderRequest(EmergencyProviderRequestStream),
    WlanBaseStationWatcherRequest(WlanBaseStationWatcherRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    syslog::init().context("failed to initialize logging")?;

    let (cobalt_api, cobalt_fut) =
        CobaltConnector::default().serve(ConnectionType::project_id(metrics::PROJECT_ID));
    let _cobalt_task = fasync::Task::spawn(cobalt_fut);
    let bss_cache = Mutex::new(RealBssCache::new());
    let bss_resolver = RealBssResolver::new(
        connect_to_service::<HttpLoaderMarker>().context("failed to connect to http loader")?,
        std::fs::read_to_string(API_KEY_FILE)
            .with_context(|| format!("failed to read {}", API_KEY_FILE))?,
    );
    let mut service_fs = ServiceFs::new_local();
    service_fs
        .dir("svc")
        .add_fidl_service(IncomingRequest::EmergencyProviderRequest)
        .add_fidl_service(IncomingRequest::WlanBaseStationWatcherRequest);
    service_fs
        .take_and_serve_directory_handle()
        .context("failed to serve outgoing namespace")?
        .for_each_concurrent(CONCURRENCY_LIMIT, |connection| {
            handle_client_requests(&bss_cache, &bss_resolver, connection, cobalt_api.clone())
                .unwrap_or_else(|e| info!("connection terminated: {:?}", e))
        })
        .await;

    Ok(())
}

async fn handle_client_requests<C: BssCache, R: BssResolver>(
    bss_cache: &Mutex<C>,
    bss_resolver: &R,
    protocol: IncomingRequest,
    cobalt_api: CobaltSender,
) -> Result<(), Error> {
    match protocol {
        IncomingRequest::EmergencyProviderRequest(client) => {
            process_location_queries(&bss_cache, bss_resolver, client, cobalt_api).await
        }
        IncomingRequest::WlanBaseStationWatcherRequest(client) => {
            process_bss_updates(&bss_cache, client, cobalt_api).await
        }
    }
}

async fn process_location_queries<C: BssCache, R: BssResolver>(
    bss_cache: &Mutex<C>,
    bss_resolver: &R,
    mut stream: EmergencyProviderRequestStream,
    mut cobalt_api: CobaltSender,
) -> Result<(), Error> {
    loop {
        match stream.try_next().await.context("failed to read emergency provider request")? {
            Some(EmergencyProviderRequest::GetCurrent { responder }) => {
                let start_time = Instant::now();
                // We don't want to hold the BSS cache lock while resolving the BSSes to a
                // `Position`, so we copy data from the iterator into our own `Vector`.
                let bss_list: Vec<(BssId, Bss)> =
                    bss_cache.lock().await.iter().map(|(&id, &bss)| (id, bss)).collect();
                match bss_resolver.resolve(bss_list).await {
                    Ok(position) => {
                        report_lookup_success_metrics(
                            &position,
                            &start_time.elapsed(),
                            &mut cobalt_api,
                        );
                        responder
                            .send(&mut Ok(position))
                            .context("failed to send position to caller")?;
                    }
                    Err(e) => {
                        info!("lookup failed: {:?}", e);
                        report_lookup_failure_metrics(e, &mut cobalt_api);
                        responder
                            .send(&mut Err(LocationError::GeneralError))
                            .context("failed to send error to client")?
                    }
                }
            }
            None => return Ok(()),
        }
    }
}

async fn process_bss_updates<C: BssCache>(
    bss_cache: &Mutex<C>,
    mut stream: WlanBaseStationWatcherRequestStream,
    mut cobalt_api: CobaltSender,
) -> Result<(), Error> {
    loop {
        match stream.try_next().await.context("failed to read base station watcher request")? {
            Some(WlanBaseStationWatcherRequest::ReportCurrentStations {
                stations,
                control_handle: _,
            }) => {
                let update_result = bss_cache
                    .lock()
                    .await
                    .update(
                        stations
                            .into_proxy()
                            .context("failed to get proxy for scan result iterator")?,
                    )
                    .await;
                report_bss_update_metrics(update_result, &mut cobalt_api);
                update_result.context("failed to apply base station update")?
            }
            None => return Ok(()),
        }
    }
}

fn report_lookup_success_metrics(
    position: &Position,
    latency: &Duration,
    cobalt_api: &mut CobaltSender,
) {
    const METERS_TO_MILLIMETERS: f64 = 1000.0;
    cobalt_api.log_event(GET_CURRENT_RESULT_METRIC_ID, GetCurrentResult::Success);
    cobalt_api.log_elapsed_time(
        GET_CURRENT_LATENCY_METRIC_ID,
        (),
        i64::try_from(latency.as_millis()).unwrap_or(i64::MAX),
    );
    cobalt_api.log_event_count(
        GET_CURRENT_ACCURACY_METRIC_ID,
        (),
        0,
        match position.extras.accuracy_meters {
            Some(accuracy_meters) => (accuracy_meters * METERS_TO_MILLIMETERS) as i64,
            None => i64::MAX,
        },
    )
}

fn report_lookup_failure_metrics(error: ResolverError, cobalt_api: &mut CobaltSender) {
    cobalt_api.log_event(GET_CURRENT_RESULT_METRIC_ID, GetCurrentResult::Failure);
    cobalt_api.log_event(
        GET_CURRENT_FAILURE_METRIC_ID,
        match error {
            ResolverError::NoBsses => GetCurrentFailure::NoBsses,
            ResolverError::Internal => GetCurrentFailure::Internal,
            ResolverError::Lookup => GetCurrentFailure::Lookup,
        },
    );
}

fn report_bss_update_metrics(result: Result<(), UpdateError>, cobalt_api: &mut CobaltSender) {
    cobalt_api.log_event(
        WLAN_SENSOR_REPORT_METRIC_ID,
        match result {
            Ok(()) => WlanSensorReportResult::Success,
            Err(UpdateError::NoBssIds) => WlanSensorReportResult::NoBssIds,
            Err(UpdateError::NoBsses) => WlanSensorReportResult::NoBsses,
            Err(UpdateError::Ipc) => WlanSensorReportResult::IpcError,
            Err(UpdateError::Service) => WlanSensorReportResult::ServiceError,
        },
    );
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        cobalt_client::traits::AsEventCode,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_cobalt::CobaltEvent,
        fuchsia_cobalt::cobalt_event_builder::CobaltEventExt,
        fuchsia_cobalt::CobaltSender,
        futures::channel::mpsc,
        futures::{future, pin_mut},
        matches::assert_matches,
        std::task::Poll,
        test_doubles::{FakeBssCache, StubBssResolver},
    };

    mod base_station_watcher {
        use {
            super::*, emergency_lib::bss_cache::UpdateError,
            fidl::endpoints::create_request_stream,
            fidl_fuchsia_location_sensor::WlanBaseStationWatcherMarker,
            fidl_fuchsia_wlan_policy::ScanResultIteratorMarker, test_case::test_case,
        };

        #[fasync::run_until_stalled(test)]
        async fn propagates_stations_downward() {
            let (cobalt_sender, _cobalt_receiver) = make_fake_cobalt_connection();
            let (proxy, stream) = create_proxy_and_stream::<WlanBaseStationWatcherMarker>()
                .expect("internal error: failed to create base station watcher");
            let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || panic!("unexpected call to resolver") },
                IncomingRequest::WlanBaseStationWatcherRequest(stream),
                cobalt_sender,
            );
            let (scan_result_reader, _scan_result_generator) =
                create_request_stream::<ScanResultIteratorMarker>()
                    .expect("internal error: failed to create scan result iterator");
            proxy
                .report_current_stations(scan_result_reader)
                .expect("internal error: proxy failed to send request");
            std::mem::drop(proxy); // Close connection so `server_fut` completes.
            assert!(server_fut.await.is_ok());
            assert!(bss_cache.lock().await.was_update_called())
        }

        #[fasync::run_until_stalled(test)]
        async fn update_error_does_not_panic() {
            let (cobalt_sender, _cobalt_receiver) = make_fake_cobalt_connection();
            let (proxy, stream) = create_proxy_and_stream::<WlanBaseStationWatcherMarker>()
                .expect("internal error: failed to create base station watcher");
            let bss_cache = Mutex::new(FakeBssCache::new(Err(UpdateError::NoBssIds)));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || panic!("unexpected call to resolver") },
                IncomingRequest::WlanBaseStationWatcherRequest(stream),
                cobalt_sender,
            );
            let (scan_result_reader, _scan_result_generator) =
                create_request_stream::<ScanResultIteratorMarker>()
                    .expect("internal error: failed to create scan result iterator");
            proxy
                .report_current_stations(scan_result_reader)
                .expect("internal error: proxy failed to send request");
            // Close connection so `server_fut` completes, even if it chooses to
            // ignore the error.
            std::mem::drop(proxy);

            // The best error handling policy isn't exactly clear: is it useful
            // to report an error upwards (which would cause `main()` to close
            // the client connection), or is it better to leave the connection
            // open, and hope that the client will provide more useful results
            // next time?
            //
            // Rather than take a position on that question, we simply validate
            // that the program doesn't crash when that happens. (If the program
            // crashed, the test framework would report a test failure with
            // the panic message.)
            let _ = server_fut.await;
        }

        #[test_case(Ok(()), WlanSensorReportResult::Success; "success")]
        #[test_case(Err(UpdateError::NoBssIds), WlanSensorReportResult::NoBssIds; "no bss ids")]
        #[test_case(Err(UpdateError::NoBsses), WlanSensorReportResult::NoBsses; "no bsses")]
        #[test_case(Err(UpdateError::Ipc), WlanSensorReportResult::IpcError; "ipc error")]
        #[test_case(Err(UpdateError::Service), WlanSensorReportResult::ServiceError;
             "service error")]
        fn reports_update_result_to_cobalt(
            update_result: Result<(), UpdateError>,
            cobalt_event: WlanSensorReportResult,
        ) {
            let test_fut = async {
                let (cobalt_sender, cobalt_receiver) = make_fake_cobalt_connection();
                let (proxy, stream) = create_proxy_and_stream::<WlanBaseStationWatcherMarker>()
                    .expect("internal error: failed to create emergency provider");
                let bss_cache = Mutex::new(FakeBssCache::new(update_result));
                let server_fut = handle_client_requests(
                    &bss_cache,
                    &StubBssResolver { resolve: || panic!("unexpected call to resolver") },
                    IncomingRequest::WlanBaseStationWatcherRequest(stream),
                    cobalt_sender,
                );
                let (scan_result_reader, _scan_result_generator) =
                    create_request_stream::<ScanResultIteratorMarker>()
                        .expect("internal error: failed to create scan result iterator");
                proxy
                    .report_current_stations(scan_result_reader)
                    .expect("internal error: proxy failed to send request");
                std::mem::drop(proxy); // Close connection so `server_fut` completes.

                let _ = server_fut.await;
                assert_eq!(
                    cobalt_receiver
                        .filter(|event| {
                            future::ready(event.metric_id == WLAN_SENSOR_REPORT_METRIC_ID)
                        })
                        .collect::<Vec<CobaltEvent>>()
                        .await,
                    vec![CobaltEvent::builder(WLAN_SENSOR_REPORT_METRIC_ID)
                        .with_event_code(cobalt_event.as_event_code())
                        .as_event()]
                );
            };
            pin_mut!(test_fut);
            assert_matches!(
                fasync::Executor::new()
                    .expect("internal error: failed to create executor")
                    .run_until_stalled(&mut test_fut),
                Poll::Ready(_)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn does_not_report_extra_metrics() {
            let (cobalt_sender, cobalt_receiver) = make_fake_cobalt_connection();
            let (proxy, stream) = create_proxy_and_stream::<WlanBaseStationWatcherMarker>()
                .expect("internal error: failed to create emergency provider");
            let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || panic!("unexpected call to resolver") },
                IncomingRequest::WlanBaseStationWatcherRequest(stream),
                cobalt_sender,
            );
            let (scan_result_reader, _scan_result_generator) =
                create_request_stream::<ScanResultIteratorMarker>()
                    .expect("internal error: failed to create scan result iterator");
            proxy
                .report_current_stations(scan_result_reader)
                .expect("internal error: proxy failed to send request");
            std::mem::drop(proxy); // Close connection so `server_fut` completes.

            let _ = server_fut.await;
            assert_eq!(
                cobalt_receiver
                    .filter(|event| future::ready(event.metric_id != WLAN_SENSOR_REPORT_METRIC_ID))
                    .collect::<Vec<CobaltEvent>>()
                    .await,
                vec![]
            );
        }
    }

    mod emergency_provider {
        use {
            super::*,
            emergency_lib::bss_resolver::ResolverError,
            fidl_fuchsia_cobalt::EventPayload::ElapsedMicros,
            fidl_fuchsia_location_position::{EmergencyProviderMarker, Position, PositionExtras},
            test_case::test_case,
        };

        const POSITION_WITH_UNKNOWN_ACCURACY: Position = Position {
            latitude: 1.0,
            longitude: -1.0,
            extras: PositionExtras { accuracy_meters: None, altitude_meters: None },
        };

        const POSITION_WITH_KNOWN_ACCURACY: Position = Position {
            latitude: 1.0,
            longitude: -1.0,
            extras: PositionExtras { accuracy_meters: Some(1.0), altitude_meters: None },
        };

        #[fasync::run_until_stalled(test)]
        async fn propagates_success_to_client() {
            let (cobalt_sender, _cobalt_receiver) = make_fake_cobalt_connection();
            let (proxy, stream) = create_proxy_and_stream::<EmergencyProviderMarker>()
                .expect("internal error: failed to create emergency provider");
            let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || Ok(POSITION_WITH_UNKNOWN_ACCURACY) },
                IncomingRequest::EmergencyProviderRequest(stream),
                cobalt_sender,
            );
            let client_fut = proxy.get_current();
            std::mem::drop(proxy); // Close connection so `server_fut` completes.

            let (client_res, _server_res) = future::join(client_fut, server_fut).await;
            assert_matches!(client_res, Ok(Ok(Position {..})))
        }

        #[fasync::run_until_stalled(test)]
        async fn propagates_error_to_client() {
            let (cobalt_sender, _cobalt_receiver) = make_fake_cobalt_connection();
            let (proxy, stream) = create_proxy_and_stream::<EmergencyProviderMarker>()
                .expect("internal error: failed to create emergency provider");
            let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || Err(ResolverError::NoBsses) },
                IncomingRequest::EmergencyProviderRequest(stream),
                cobalt_sender,
            );
            let client_fut = proxy.get_current();
            std::mem::drop(proxy); // Close connection so `server_fut` completes.

            let (client_res, _server_res) = future::join(client_fut, server_fut).await;
            assert_matches!(client_res, Ok(Err(_))) // The `Ok` is the FIDL-level result.
        }

        #[fasync::run_until_stalled(test)]
        async fn reports_success_to_cobalt() {
            let (cobalt_sender, cobalt_receiver) = make_fake_cobalt_connection();
            let (proxy, stream) = create_proxy_and_stream::<EmergencyProviderMarker>()
                .expect("internal error: failed to create emergency provider");
            let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || Ok(POSITION_WITH_UNKNOWN_ACCURACY) },
                IncomingRequest::EmergencyProviderRequest(stream),
                cobalt_sender,
            );
            let _ = proxy.get_current();
            std::mem::drop(proxy); // Close connection so `server_fut` completes.

            let _ = server_fut.await;
            assert_eq!(
                cobalt_receiver
                    .filter(|event| future::ready(event.metric_id == GET_CURRENT_RESULT_METRIC_ID))
                    .collect::<Vec<CobaltEvent>>()
                    .await,
                vec![CobaltEvent::builder(GET_CURRENT_RESULT_METRIC_ID)
                    .with_event_code(GetCurrentResult::Success.as_event_code())
                    .as_event()]
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn reports_failure_to_cobalt() {
            let (cobalt_sender, cobalt_receiver) = make_fake_cobalt_connection();
            let (proxy, stream) = create_proxy_and_stream::<EmergencyProviderMarker>()
                .expect("internal error: failed to create emergency provider");
            let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || Err(ResolverError::NoBsses) },
                IncomingRequest::EmergencyProviderRequest(stream),
                cobalt_sender,
            );
            let _ = proxy.get_current();
            std::mem::drop(proxy); // Close connection so `server_fut` completes.

            let _ = server_fut.await;
            assert_eq!(
                cobalt_receiver
                    .filter(|event| future::ready(event.metric_id == GET_CURRENT_RESULT_METRIC_ID))
                    .collect::<Vec<CobaltEvent>>()
                    .await,
                vec![CobaltEvent::builder(GET_CURRENT_RESULT_METRIC_ID)
                    .with_event_code(GetCurrentResult::Failure.as_event_code())
                    .as_event()]
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn propagates_reported_accuracy_to_cobalt() {
            let (cobalt_sender, cobalt_receiver) = make_fake_cobalt_connection();
            let (proxy, stream) = create_proxy_and_stream::<EmergencyProviderMarker>()
                .expect("internal error: failed to create emergency provider");
            let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || Ok(POSITION_WITH_KNOWN_ACCURACY) },
                IncomingRequest::EmergencyProviderRequest(stream),
                cobalt_sender,
            );
            let _ = proxy.get_current();
            std::mem::drop(proxy); // Close connection so `server_fut` completes.

            let _ = server_fut.await;
            assert_eq!(
                cobalt_receiver
                    .filter(|event| future::ready(
                        event.metric_id == GET_CURRENT_ACCURACY_METRIC_ID
                    ))
                    .collect::<Vec<CobaltEvent>>()
                    .await,
                vec![CobaltEvent::builder(GET_CURRENT_ACCURACY_METRIC_ID).as_count_event(
                    0,
                    1000 // In millimeters, for higher precision
                )]
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn reports_worst_accuracy_if_accuracy_is_unknown() {
            let (cobalt_sender, cobalt_receiver) = make_fake_cobalt_connection();
            let (proxy, stream) = create_proxy_and_stream::<EmergencyProviderMarker>()
                .expect("internal error: failed to create emergency provider");
            let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || Ok(POSITION_WITH_UNKNOWN_ACCURACY) },
                IncomingRequest::EmergencyProviderRequest(stream),
                cobalt_sender,
            );
            let _ = proxy.get_current();
            std::mem::drop(proxy); // Close connection so `server_fut` completes.

            let _ = server_fut.await;
            assert_eq!(
                cobalt_receiver
                    .filter(|event| future::ready(
                        event.metric_id == GET_CURRENT_ACCURACY_METRIC_ID
                    ))
                    .collect::<Vec<CobaltEvent>>()
                    .await,
                vec![CobaltEvent::builder(GET_CURRENT_ACCURACY_METRIC_ID)
                    .as_count_event(0, i64::MAX)]
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn reports_elapsed_time_to_cobalt_on_success() {
            let (cobalt_sender, cobalt_receiver) = make_fake_cobalt_connection();
            let (proxy, stream) = create_proxy_and_stream::<EmergencyProviderMarker>()
                .expect("internal error: failed to create emergency provider");
            let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || Ok(POSITION_WITH_UNKNOWN_ACCURACY) },
                IncomingRequest::EmergencyProviderRequest(stream),
                cobalt_sender,
            );
            let _ = proxy.get_current();
            std::mem::drop(proxy); // Close connection so `server_fut` completes.

            let _ = server_fut.await;
            let latency_reports = cobalt_receiver
                .filter(|event| future::ready(event.metric_id == GET_CURRENT_LATENCY_METRIC_ID))
                .collect::<Vec<CobaltEvent>>()
                .await;
            assert_eq!(latency_reports.len(), 1);
            assert_matches!(
                latency_reports[0],
                CobaltEvent {
                    metric_id: GET_CURRENT_LATENCY_METRIC_ID,
                    payload: ElapsedMicros(_),
                    ..
                }
            );
        }

        #[test_case(ResolverError::NoBsses, GetCurrentFailure::NoBsses; "no bsses")]
        #[test_case(ResolverError::Internal, GetCurrentFailure::Internal; "internal")]
        #[test_case(ResolverError::Lookup, GetCurrentFailure::Lookup; "lookup")]
        fn reports_resolver_error_to_cobalt(
            resolver_error: ResolverError,
            cobalt_error: GetCurrentFailure,
        ) {
            let test_fut = async {
                let (cobalt_sender, cobalt_receiver) = make_fake_cobalt_connection();
                let (proxy, stream) = create_proxy_and_stream::<EmergencyProviderMarker>()
                    .expect("internal error: failed to create emergency provider");
                let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
                let resolver = StubBssResolver { resolve: || Err(resolver_error) };
                let server_fut = handle_client_requests(
                    &bss_cache,
                    &resolver,
                    IncomingRequest::EmergencyProviderRequest(stream),
                    cobalt_sender,
                );
                let _ = proxy.get_current();
                std::mem::drop(proxy); // Close connection so `server_fut` completes.

                let _ = server_fut.await;
                assert_eq!(
                    cobalt_receiver
                        .filter(|event| {
                            future::ready(event.metric_id == GET_CURRENT_FAILURE_METRIC_ID)
                        })
                        .collect::<Vec<CobaltEvent>>()
                        .await,
                    vec![CobaltEvent::builder(GET_CURRENT_FAILURE_METRIC_ID)
                        .with_event_code(cobalt_error.as_event_code())
                        .as_event()]
                );
            };
            pin_mut!(test_fut);
            assert_matches!(
                fasync::Executor::new()
                    .expect("internal error: failed to create executor")
                    .run_until_stalled(&mut test_fut),
                Poll::Ready(_)
            );
        }

        #[test_case(
            Ok(()),
            &[GET_CURRENT_RESULT_METRIC_ID, GET_CURRENT_ACCURACY_METRIC_ID,
                 GET_CURRENT_LATENCY_METRIC_ID];
            "on_success")]
        #[test_case(
            Err(()),
            &[GET_CURRENT_RESULT_METRIC_ID, GET_CURRENT_FAILURE_METRIC_ID];
            "on_failure")]
        fn does_not_report_extra_metrics(result: Result<(), ()>, expected_metric_ids: &[u32]) {
            let test_fut = async {
                let (cobalt_sender, cobalt_receiver) = make_fake_cobalt_connection();
                let (proxy, stream) = create_proxy_and_stream::<EmergencyProviderMarker>()
                    .expect("internal error: failed to create emergency provider");
                let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
                let resolver = StubBssResolver {
                    resolve: || match &result {
                        Ok(()) => Ok(POSITION_WITH_KNOWN_ACCURACY),
                        Err(()) => Err(ResolverError::NoBsses),
                    },
                };
                let server_fut = handle_client_requests(
                    &bss_cache,
                    &resolver,
                    IncomingRequest::EmergencyProviderRequest(stream),
                    cobalt_sender,
                );
                let _ = proxy.get_current();
                std::mem::drop(proxy); // Close connection so `server_fut` completes.

                let _ = server_fut.await;
                assert_eq!(
                    cobalt_receiver
                        .filter(|event| future::ready(
                            expected_metric_ids.iter().find(|&&i| i == event.metric_id).is_none()
                        ))
                        .collect::<Vec<CobaltEvent>>()
                        .await,
                    vec![]
                );
            };
            pin_mut!(test_fut);
            assert_matches!(
                fasync::Executor::new()
                    .expect("internal error: failed to create executor")
                    .run_until_stalled(&mut test_fut),
                Poll::Ready(_)
            );
        }
    }

    fn make_fake_cobalt_connection() -> (CobaltSender, mpsc::Receiver<CobaltEvent>) {
        const MAX_METRICS_PER_QUERY: usize = 3;
        const MAX_QUERIES: usize = 1;
        let (sender, receiver) = mpsc::channel(MAX_METRICS_PER_QUERY * MAX_QUERIES);
        (CobaltSender::new(sender), receiver)
    }
}

#[cfg(test)]
mod test_doubles {
    use {
        super::*,
        async_trait::async_trait,
        emergency_lib::{bss_cache::UpdateError, bss_resolver::ResolverError},
        fidl_fuchsia_location_position::Position,
        fidl_fuchsia_wlan_policy::ScanResultIteratorProxyInterface,
    };

    pub(super) struct FakeBssCache {
        update_result: Result<(), UpdateError>,
        bsses: Vec<(BssId, Bss)>,
        was_update_called: bool,
    }

    pub(super) struct StubBssResolver<R: Fn() -> Result<Position, ResolverError>> {
        // Note, we can't just store a value here, because `Position` is not Copy.
        pub resolve: R,
    }

    impl FakeBssCache {
        pub fn new(update_result: Result<(), UpdateError>) -> Self {
            Self { update_result, bsses: Vec::new(), was_update_called: false }
        }

        pub fn was_update_called(&self) -> bool {
            self.was_update_called
        }
    }

    #[async_trait(?Send)]
    impl BssCache for FakeBssCache {
        async fn update<I: ScanResultIteratorProxyInterface>(
            &mut self,
            _new_bsses: I,
        ) -> Result<(), UpdateError> {
            self.was_update_called = true;
            self.update_result.clone()
        }

        fn iter(&self) -> Box<dyn Iterator<Item = (&'_ BssId, &'_ Bss)> + '_> {
            Box::new(self.bsses.iter().map(|(id, bss)| (id, bss)))
        }
    }

    #[async_trait(?Send)]
    impl<R> BssResolver for StubBssResolver<R>
    where
        R: Fn() -> Result<Position, ResolverError>,
    {
        async fn resolve<'a, I, T, U>(&self, _bss_list: I) -> Result<Position, ResolverError> {
            (self.resolve)()
        }
    }
}
