// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Context, Error},
    emergency_lib::{
        bss_cache::{Bss, BssCache, BssId, RealBssCache},
        bss_resolver::{BssResolver, RealBssResolver},
    },
    fidl_fuchsia_location::Error as LocationError,
    fidl_fuchsia_location_position::{EmergencyProviderRequest, EmergencyProviderRequestStream},
    fidl_fuchsia_location_sensor::{
        WlanBaseStationWatcherRequest, WlanBaseStationWatcherRequestStream,
    },
    fidl_fuchsia_net_http::LoaderMarker as HttpLoaderMarker,
    fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_service, server::ServiceFs},
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::{lock::Mutex, prelude::*},
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
            handle_client_requests(&bss_cache, &bss_resolver, connection)
                .unwrap_or_else(|e| fx_log_info!("connection terminated: {:?}", e))
        })
        .await;

    Ok(())
}

async fn handle_client_requests<C: BssCache, R: BssResolver>(
    bss_cache: &Mutex<C>,
    bss_resolver: &R,
    protocol: IncomingRequest,
) -> Result<(), Error> {
    match protocol {
        IncomingRequest::EmergencyProviderRequest(client) => {
            process_location_queries(&bss_cache, bss_resolver, client).await
        }
        IncomingRequest::WlanBaseStationWatcherRequest(client) => {
            process_bss_updates(&bss_cache, client).await
        }
    }
}

async fn process_location_queries<C: BssCache, R: BssResolver>(
    bss_cache: &Mutex<C>,
    bss_resolver: &R,
    mut stream: EmergencyProviderRequestStream,
) -> Result<(), Error> {
    loop {
        match stream.try_next().await.context("failed to read emergency provider request")? {
            Some(EmergencyProviderRequest::GetCurrent { responder }) => {
                // We don't want to hold the BSS cache lock while resolving the BSSes to a
                // `Position`, so we copy data from the iterator into our own `Vector`.
                let bss_list: Vec<(BssId, Bss)> =
                    bss_cache.lock().await.iter().map(|(&id, &bss)| (id, bss)).collect();
                match bss_resolver.resolve(bss_list).await {
                    Ok(position) => responder
                        .send(&mut Ok(position))
                        .context("failed to send position to caller")?,
                    Err(e) => {
                        fx_log_info!("lookup failed: {:?}", e);
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
) -> Result<(), Error> {
    loop {
        match stream.try_next().await.context("failed to read base station watcher request")? {
            Some(WlanBaseStationWatcherRequest::ReportCurrentStations {
                stations,
                control_handle: _,
            }) => bss_cache
                .lock()
                .await
                .update(
                    stations
                        .into_proxy()
                        .context("failed to get proxy for scan result iterator")?,
                )
                .await
                .context("failed to apply base station update")?,
            None => return Ok(()),
        }
    }
}

#[cfg(test)]
mod tests {
    mod base_station_watcher {
        use {
            super::super::{
                test_doubles::{FakeBssCache, StubBssResolver},
                *,
            },
            emergency_lib::bss_cache::UpdateError,
            fidl::endpoints::{create_proxy_and_stream, create_request_stream},
            fidl_fuchsia_location_sensor::WlanBaseStationWatcherMarker,
            fidl_fuchsia_wlan_policy::ScanResultIteratorMarker,
        };

        #[fasync::run_until_stalled(test)]
        async fn propagates_stations_downward() {
            let (proxy, stream) = create_proxy_and_stream::<WlanBaseStationWatcherMarker>()
                .expect("internal error: failed to create base station watcher");
            let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || panic!("unexpected call to resolver") },
                IncomingRequest::WlanBaseStationWatcherRequest(stream),
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
            let (proxy, stream) = create_proxy_and_stream::<WlanBaseStationWatcherMarker>()
                .expect("internal error: failed to create base station watcher");
            let bss_cache = Mutex::new(FakeBssCache::new(Err(UpdateError::NoBssIds)));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || panic!("unexpected call to resolver") },
                IncomingRequest::WlanBaseStationWatcherRequest(stream),
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
    }

    mod emergency_provider {
        use {
            super::super::{
                test_doubles::{FakeBssCache, StubBssResolver},
                *,
            },
            emergency_lib::bss_resolver::ResolverError,
            fidl::endpoints::create_proxy_and_stream,
            fidl_fuchsia_location_position::{EmergencyProviderMarker, Position, PositionExtras},
            futures::future,
            matches::assert_matches,
        };

        #[fasync::run_until_stalled(test)]
        async fn propagates_success_to_client() {
            let (proxy, stream) = create_proxy_and_stream::<EmergencyProviderMarker>()
                .expect("internal error: failed to create emergency provider");
            let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver {
                    resolve: || {
                        Ok(Position {
                            latitude: 1.0,
                            longitude: -1.0,
                            extras: PositionExtras { accuracy_meters: None, altitude_meters: None },
                        })
                    },
                },
                IncomingRequest::EmergencyProviderRequest(stream),
            );
            let client_fut = proxy.get_current();
            std::mem::drop(proxy); // Close connection so `server_fut` completes.

            let (client_res, _server_res) = future::join(client_fut, server_fut).await;
            assert_matches!(client_res, Ok(Ok(Position {..})))
        }

        #[fasync::run_until_stalled(test)]
        async fn propagates_error_to_client() {
            let (proxy, stream) = create_proxy_and_stream::<EmergencyProviderMarker>()
                .expect("internal error: failed to create emergency provider");
            let bss_cache = Mutex::new(FakeBssCache::new(Ok(())));
            let server_fut = handle_client_requests(
                &bss_cache,
                &StubBssResolver { resolve: || Err(ResolverError::NoBsses) },
                IncomingRequest::EmergencyProviderRequest(stream),
            );
            let client_fut = proxy.get_current();
            std::mem::drop(proxy); // Close connection so `server_fut` completes.

            let (client_res, _server_res) = future::join(client_fut, server_fut).await;
            assert_matches!(client_res, Ok(Err(_))) // The `Ok` is the FIDL-level result.
        }
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
