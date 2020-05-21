// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Manages Scan requests for the Client Policy API.
use {
    crate::{client::ClientPtr, util::sme_conversion::security_from_sme_protection},
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_location_sensor as fidl_location_sensor, fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_policy as fidl_policy, fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_component::client::connect_to_service,
    futures::{prelude::*, stream::FuturesUnordered},
    log::{debug, error, info},
    std::collections::HashMap,
};

// Arbitrary count of networks (ssid/security pairs) to output per request
const OUTPUT_CHUNK_NETWORK_COUNT: usize = 5;

// An internal version of fidl_policy::Bss that can be cloned
#[derive(Clone)]
struct Bss {
    /// MAC address for the AP interface.
    bssid: [u8; 6],
    /// Calculated received signal strength for the beacon/probe response.
    rssi: i8,
    /// Operating frequency for this network (in MHz).
    frequency: u32,
    /// Realtime timestamp for this scan result entry.
    timestamp_nanos: i64,
}
impl From<&fidl_sme::BssInfo> for Bss {
    fn from(bss: &fidl_sme::BssInfo) -> Bss {
        Bss {
            bssid: bss.bssid,
            rssi: bss.rx_dbm,
            frequency: 0,       // TODO(mnck): convert channel to freq
            timestamp_nanos: 0, // TODO(mnck): find where this comes from
        }
    }
}
impl From<Bss> for fidl_policy::Bss {
    fn from(input: Bss) -> Self {
        fidl_policy::Bss {
            bssid: Some(input.bssid),
            rssi: Some(input.rssi),
            frequency: Some(input.frequency),
            timestamp_nanos: Some(input.timestamp_nanos),
        }
    }
}

// An internal version of fidl_policy::ScanResult that can be cloned
#[derive(Clone)]
struct ScanResult {
    /// Network properties used to distinguish between networks and to group
    /// individual APs.
    id: fidl_policy::NetworkIdentifier,
    /// Individual access points offering the specified network.
    entries: Vec<Bss>,
    /// Indication if the detected network is supported by the implementation.
    compatibility: fidl_policy::Compatibility,
}
impl From<ScanResult> for fidl_policy::ScanResult {
    fn from(input: ScanResult) -> Self {
        fidl_policy::ScanResult {
            id: Some(input.id),
            entries: Some(input.entries.into_iter().map(fidl_policy::Bss::from).collect()),
            compatibility: Some(input.compatibility),
        }
    }
}

/// Handles incoming scan requests by creating a new SME scan request.
/// For the output_iterator, returns scan results and/or errors.
/// On successful scan, calls get_successful_scan_observers() and provides scan results to each.
pub async fn perform_scan<F>(
    client: ClientPtr,
    output_iterator: fidl::endpoints::ServerEnd<fidl_policy::ScanResultIteratorMarker>,
    get_successful_scan_observers: F,
) where
    F: FnOnce() -> Vec<fidl::endpoints::ServerEnd<fidl_policy::ScanResultIteratorMarker>>,
{
    match request_sme_scan(client).await {
        Ok(txn) => {
            info!("Sent scan request to SME successfully");
            let mut stream = txn.take_event_stream();
            let mut scanned_networks = vec![];
            while let Some(Ok(event)) = stream.next().await {
                match event {
                    fidl_sme::ScanTransactionEvent::OnResult { aps: new_aps } => {
                        debug!("Received scan results from SME");
                        scanned_networks.extend(new_aps);
                    }
                    fidl_sme::ScanTransactionEvent::OnFinished {} => {
                        info!("Finished getting scan results from SME");
                        let scan_results = convert_scan_info(&scanned_networks);

                        let mut futures = FuturesUnordered::new();
                        let log_and_discard_error =
                            |e| error!("Failed to send scan results to consumer: {:?}", e);

                        // Send the results to the original requester
                        futures.push(
                            send_scan_results(output_iterator, &scan_results)
                                .map_err(log_and_discard_error),
                        );

                        // Send the results to all consumers of the successful scan results
                        get_successful_scan_observers().drain(..).for_each(|output| {
                            futures.push(
                                send_scan_results(output, &scan_results)
                                    .map_err(log_and_discard_error),
                            );
                        });

                        // Loop until everyone is done consuming results
                        while let Some(_) = futures.next().await {}
                        return;
                    }
                    fidl_sme::ScanTransactionEvent::OnError { error } => {
                        error!("Scan error from SME: {:?}", error);
                        send_scan_error(output_iterator, fidl_policy::ScanErrorCode::GeneralError)
                            .await
                            .unwrap_or_else(|e| error!("Failed to send scan results: {}", e));
                        return;
                    }
                };
            }
        }
        Err(error) => {
            error!("Failed to send scan request to SME: {:?}", error);
            send_scan_error(output_iterator, fidl_policy::ScanErrorCode::GeneralError)
                .await
                .unwrap_or_else(|e| error!("Failed to send scan results: {}", e));
        }
    };
}

fn clone_sme_bss_info(bss: &fidl_sme::BssInfo) -> fidl_sme::BssInfo {
    fidl_sme::BssInfo {
        bssid: bss.bssid.clone(),
        ssid: bss.ssid.clone(),
        rx_dbm: bss.rx_dbm,
        snr_db: bss.snr_db,
        channel: bss.channel,
        protection: bss.protection,
        compatible: bss.compatible,
    }
}

/// When a successful scan takes place, the scan results should be provided
/// to several "observers". These observers will use the scan results
/// for different purposes, e.g. determining network quality.
pub fn get_successful_scan_observers(
) -> Vec<fidl::endpoints::ServerEnd<fidl_policy::ScanResultIteratorMarker>> {
    /// The location sensor module uses scan results to help determine the
    /// device's location, for use by the Emergency Location Provider.
    fn get_location_sensor_output_iterator(
    ) -> Result<fidl::endpoints::ServerEnd<fidl_policy::ScanResultIteratorMarker>, Error> {
        let (iter, server) =
            fidl::endpoints::create_endpoints::<fidl_policy::ScanResultIteratorMarker>()
                .map_err(|err| format_err!("failed to create iterator: {:?}", err))?;
        let location_watcher_proxy = connect_to_service::<
            fidl_location_sensor::WlanBaseStationWatcherMarker,
        >()
        .map_err(|err| format_err!("failed to connect to location sensor service: {:?}", err))?;
        location_watcher_proxy
            .report_current_stations(iter)
            .map_err(|err| format_err!("failed to call location sensor service: {:?}", err))?;
        return Ok(server);
    }

    // Get the iterators for each of the observers
    vec![get_location_sensor_output_iterator()]
        .drain(..)
        // Filter out any errors and just log a message.
        // No error recovery, we'll just try again next time a scan result comes in.
        .filter_map(|result| match result {
            Err(e) => {
                error!("{:?}", e);
                None
            }
            Ok(iterator) => Some(iterator),
        })
        .collect()
}

/// Converts array of fidl_sme::BssInfo to array of internal ScanResult.
/// There is one BssInfo per BSSID. In contrast, there is one ScanResult per
/// SSID, with information for multiple BSSs contained within it.
fn convert_scan_info(scanned_networks: &[fidl_sme::BssInfo]) -> Vec<ScanResult> {
    let mut bss_by_network: HashMap<fidl_policy::NetworkIdentifier, Vec<fidl_sme::BssInfo>> =
        HashMap::new();
    for bss in scanned_networks.iter() {
        if let Some(security) = security_from_sme_protection(bss.protection) {
            bss_by_network
                .entry(fidl_policy::NetworkIdentifier { ssid: bss.ssid.to_vec(), type_: security })
                .or_insert(vec![])
                .push(clone_sme_bss_info(&bss));
        } else {
            // TODO(mnck): log a metric here
            error!("Unknown security type: {:?}", bss.protection);
        }
    }
    let mut scan_results: Vec<ScanResult> = bss_by_network
        .iter()
        .map(|(network, bss_infos)| ScanResult {
            id: network.clone(),
            entries: bss_infos.iter().map(Bss::from).collect(),
            compatibility: if bss_infos.iter().any(|bss| bss.compatible) {
                fidl_policy::Compatibility::Supported
            } else {
                fidl_policy::Compatibility::DisallowedNotSupported
            },
        })
        .collect();

    scan_results.sort_by(|a, b| a.id.ssid.cmp(&b.id.ssid));
    return scan_results;
}

/// Send batches of results to the output iterator when getNext() is called on it.
/// Close the channel when no results are remaining.
async fn send_scan_results(
    output_iterator: fidl::endpoints::ServerEnd<fidl_policy::ScanResultIteratorMarker>,
    scan_results: &Vec<ScanResult>,
) -> Result<(), fidl::Error> {
    let mut chunks = scan_results.chunks(OUTPUT_CHUNK_NETWORK_COUNT);
    // Wait to get a request for a chunk of scan results
    let (mut stream, ctrl) = output_iterator.into_stream_and_control_handle()?;
    loop {
        if let Some(req) = stream.try_next().await? {
            let fidl_policy::ScanResultIteratorRequest::GetNext { responder } = req;
            if let Some(chunk) = chunks.next() {
                let mut next_result: fidl_policy::ScanResultIteratorGetNextResult = Ok(chunk
                    .into_iter()
                    .map(|r| fidl_policy::ScanResult::from(r.clone()))
                    .collect());
                responder.send(&mut next_result)?;
            } else {
                // When no results are left, send an empty vec and close the channel.
                let mut next_result: fidl_policy::ScanResultIteratorGetNextResult = Ok(vec![]);
                responder.send(&mut next_result)?;
                ctrl.shutdown();
                break;
            }
        } else {
            // This will happen if the iterator request stream was closed and we expected to send
            // another response.
            info!("Peer closed channel for getting scan results unexpectedly");
            break;
        }
    }
    Ok(())
}

/// On the next request for results, send an error to the output iterator and
/// shut it down.
async fn send_scan_error(
    output_iterator: fidl::endpoints::ServerEnd<fidl_policy::ScanResultIteratorMarker>,
    error_code: fidl_policy::ScanErrorCode,
) -> Result<(), fidl::Error> {
    // Wait to get a request for a chunk of scan results
    let (mut stream, ctrl) = output_iterator.into_stream_and_control_handle()?;
    if let Some(req) = stream.try_next().await? {
        let fidl_policy::ScanResultIteratorRequest::GetNext { responder } = req;
        let mut err: fidl_policy::ScanResultIteratorGetNextResult = Err(error_code);
        responder.send(&mut err)?;
        ctrl.shutdown();
    } else {
        // This will happen if the iterator request stream was closed and we expected to send
        // another response.
        info!("Peer closed channel for getting scan results unexpectedly");
    }
    Ok(())
}

/// Attempts to issue a new scan request to the currently active SME.
async fn request_sme_scan(client: ClientPtr) -> Result<fidl_sme::ScanTransactionProxy, Error> {
    let client = client.lock().await;
    let client_sme =
        client.access_sme().ok_or_else(|| format_err!("no active client interface"))?;

    let mut request =
        fidl_sme::ScanRequest { timeout: 5, scan_type: fidl_common::ScanType::Passive };
    let (local, remote) =
        fidl::endpoints::create_proxy().context(format!("failed to create sme proxy"))?;
    client_sme.scan(&mut request, remote).context(format!("failed to connect to sme"))?;
    Ok(local)
}

#[cfg(test)]
mod tests {
    use {
        super::{super::Client, *},
        crate::util::logger::set_logger_for_test,
        fidl::endpoints::create_proxy,
        fuchsia_async as fasync,
        futures::{lock::Mutex, task::Poll},
        pin_utils::pin_mut,
        std::sync::Arc,
        wlan_common::assert_variant,
    };

    /// Creates a Client wrapper.
    async fn create_client() -> (ClientPtr, fidl_sme::ClientSmeRequestStream) {
        set_logger_for_test();
        let (client_sme, remote) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("error creating proxy");
        let client = Arc::new(Mutex::new(Client::from(client_sme)));
        (client, remote.into_stream().expect("failed to create stream"))
    }

    fn send_sme_scan_result(
        exec: &mut fasync::Executor,
        sme_stream: &mut fidl_sme::ClientSmeRequestStream,
        scan_results: &[fidl_sme::BssInfo],
    ) {
        // Check that a scan request was sent to the sme and send back results
        assert_variant!(
            exec.run_until_stalled(&mut sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                txn, ..
            }))) => {
                // Send all the APs
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_result(&mut scan_results.to_vec().iter_mut())
                    .expect("failed to send scan data");

                // Send the end of data
                ctrl.send_on_finished()
                    .expect("failed to send scan data");
            }
        );
    }

    // Creates test data for the scan functions.
    fn create_scan_ap_data() -> (Vec<fidl_sme::BssInfo>, Vec<fidl_policy::ScanResult>) {
        let input_aps = vec![
            fidl_sme::BssInfo {
                bssid: [0, 0, 0, 0, 0, 0],
                ssid: "duplicated ssid".as_bytes().to_vec(),
                rx_dbm: 0,
                snr_db: 0,
                channel: 0,
                protection: fidl_sme::Protection::Wpa3Enterprise,
                compatible: true,
            },
            fidl_sme::BssInfo {
                bssid: [1, 2, 3, 4, 5, 6],
                ssid: "unique ssid".as_bytes().to_vec(),
                rx_dbm: 7,
                snr_db: 0,
                channel: 8,
                protection: fidl_sme::Protection::Wpa2Personal,
                compatible: true,
            },
            fidl_sme::BssInfo {
                bssid: [7, 8, 9, 10, 11, 12],
                ssid: "duplicated ssid".as_bytes().to_vec(),
                rx_dbm: 13,
                snr_db: 0,
                channel: 14,
                protection: fidl_sme::Protection::Wpa3Enterprise,
                compatible: false,
            },
        ];
        // input_aps contains some duplicate SSIDs, which should be
        // grouped in the output.
        let output_aps = vec![
            fidl_policy::ScanResult {
                id: Some(fidl_policy::NetworkIdentifier {
                    ssid: "duplicated ssid".as_bytes().to_vec(),
                    type_: fidl_policy::SecurityType::Wpa3,
                }),
                entries: Some(vec![
                    fidl_policy::Bss {
                        bssid: Some([0, 0, 0, 0, 0, 0]),
                        rssi: Some(0),
                        frequency: Some(0),
                        timestamp_nanos: Some(0),
                    },
                    fidl_policy::Bss {
                        bssid: Some([7, 8, 9, 10, 11, 12]),
                        rssi: Some(13),
                        frequency: Some(0),
                        timestamp_nanos: Some(0),
                    },
                ]),
                compatibility: Some(fidl_policy::Compatibility::Supported),
            },
            fidl_policy::ScanResult {
                id: Some(fidl_policy::NetworkIdentifier {
                    ssid: "unique ssid".as_bytes().to_vec(),
                    type_: fidl_policy::SecurityType::Wpa2,
                }),
                entries: Some(vec![fidl_policy::Bss {
                    bssid: Some([1, 2, 3, 4, 5, 6]),
                    rssi: Some(7),
                    frequency: Some(0),
                    timestamp_nanos: Some(0),
                }]),
                compatibility: Some(fidl_policy::Compatibility::Supported),
            },
        ];
        (input_aps, output_aps)
    }

    #[test]
    fn basic_scan() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (client, mut sme_stream) = exec.run_singlethreaded(create_client());

        // Issue request to scan.
        let (iter, iter_server) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        let scan_fut = perform_scan(client, iter_server, || vec![]);
        pin_mut!(scan_fut);

        // Request a chunk of scan results. Progress until waiting on response from server side of
        // the iterator.
        let mut output_iter_fut = iter.get_next();
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut), Poll::Pending);
        // Progress scan handler forward so that it will respond to the iterator get next request.
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Create mock scan data and send it via the SME
        let (input_aps, output_aps) = create_scan_ap_data();
        send_sme_scan_result(&mut exec, &mut sme_stream, &input_aps);

        // Process response from SME
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Check for results
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results, output_aps);
        });

        // Request the next chunk of scan results. Progress until waiting on response from server side of
        // the iterator.
        let mut output_iter_fut = iter.get_next();

        // Process scan handler
        // Note: this will be Poll::Ready because the scan handler will exit after sending the final
        // scan results.
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Ready(()));

        // Check for results
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results, vec![]);
        });
    }

    #[test]
    fn scan_iterator_never_polled() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (client, mut sme_stream) = exec.run_singlethreaded(create_client());

        // Issue request to scan.
        let (_iter, iter_server) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        let scan_fut = perform_scan(client.clone(), iter_server, || vec![]);
        pin_mut!(scan_fut);

        // Progress scan side forward without ever calling getNext() on the scan result iterator
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Create mock scan data and send it via the SME
        let (input_aps, output_aps) = create_scan_ap_data();
        send_sme_scan_result(&mut exec, &mut sme_stream, &input_aps);

        // Progress scan side forward without progressing the scan result iterator
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Issue a second request to scan, to make sure that everything is still
        // moving along even though the first scan result iterator was never progressed.
        let (iter2, iter_server2) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        let scan_fut2 = perform_scan(client, iter_server2, || vec![]);
        pin_mut!(scan_fut2);

        // Progress scan side forward
        assert_variant!(exec.run_until_stalled(&mut scan_fut2), Poll::Pending);

        // Create mock scan data and send it via the SME
        send_sme_scan_result(&mut exec, &mut sme_stream, &input_aps);

        // Request the results on the second iterator
        let mut output_iter_fut2 = iter2.get_next();

        // Progress scan side forward
        assert_variant!(exec.run_until_stalled(&mut scan_fut2), Poll::Pending);

        // Ensure results are present on the iterator
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut2), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results, output_aps);
        });
    }

    #[test]
    fn scan_iterator_shut_down() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (client, mut sme_stream) = exec.run_singlethreaded(create_client());

        // Issue request to scan.
        let (iter, iter_server) =
            fidl::endpoints::create_endpoints().expect("failed to create iterator");
        let scan_fut = perform_scan(client, iter_server, || vec![]);
        pin_mut!(scan_fut);

        // Progress scan handler forward so that it will respond to the iterator get next request.
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Create mock scan data and send it via the SME
        let (input_aps, _) = create_scan_ap_data();
        send_sme_scan_result(&mut exec, &mut sme_stream, &input_aps);

        // Close the channel
        drop(iter.into_channel());

        // Process scan handler
        // Note: this will be Poll::Ready because the scan handler will exit since all the consumers are done
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Ready(()));
    }

    #[test]
    fn scan_error() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (client, mut sme_stream) = exec.run_singlethreaded(create_client());

        // Issue request to scan.
        let (iter, iter_server) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        let scan_fut = perform_scan(client, iter_server, || vec![]);
        pin_mut!(scan_fut);

        // Request a chunk of scan results. Progress until waiting on response from server side of
        // the iterator.
        let mut output_iter_fut = iter.get_next();
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut), Poll::Pending);
        // Progress scan handler forward so that it will respond to the iterator get next request.
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Check that a scan request was sent to the sme and send back an error
        assert_variant!(
            exec.run_until_stalled(&mut sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                txn, ..
            }))) => {
                // Send failed scan response.
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_error(&mut fidl_sme::ScanError {
                    code: fidl_sme::ScanErrorCode::InternalError,
                    message: "Failed to scan".to_string()
                })
                    .expect("failed to send scan error");
            }
        );

        // Process SME result.
        // Note: this will be Poll::Ready, since the scan handler will quit after sending the error
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Ready(()));

        // the iterator should have an error on it
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results");
            assert_eq!(results, Err(fidl_policy::ScanErrorCode::GeneralError));
        });
    }

    #[test]
    fn overlapping_scans() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (client, mut sme_stream) = exec.run_singlethreaded(create_client());
        let (input_aps, output_aps) = create_scan_ap_data();

        // Create two sets of endpoints
        let (iter0, iter_server0) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        let (iter1, iter_server1) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");

        // Issue request to scan on both iterator.
        let scan_fut0 = perform_scan(client.clone(), iter_server0, || vec![]);
        pin_mut!(scan_fut0);
        let scan_fut1 = perform_scan(client, iter_server1, || vec![]);
        pin_mut!(scan_fut1);

        // Request a chunk of scan results on both iterators. Progress until waiting on
        // response from server side of the iterator.
        let mut output_iter_fut0 = iter0.get_next();
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut0), Poll::Pending);
        let mut output_iter_fut1 = iter1.get_next();
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut1), Poll::Pending);

        // Progress first scan handler forward so that it will respond to the iterator get next request.
        assert_variant!(exec.run_until_stalled(&mut scan_fut0), Poll::Pending);

        // Check that a scan request was sent to the sme and send back results
        assert_variant!(
            exec.run_until_stalled(&mut sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                txn, ..
            }))) => {
                // Send the first AP
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                let mut aps = [input_aps[0].clone()];
                ctrl.send_on_result(&mut aps.iter_mut())
                    .expect("failed to send scan data");
                // Process SME result.
                assert_variant!(exec.run_until_stalled(&mut scan_fut0), Poll::Pending);
                // The iterator should not have any data yet, until the sme is done
                assert_variant!(exec.run_until_stalled(&mut output_iter_fut0), Poll::Pending);

                // Progress second scan handler forward so that it will respond to the iterator get next request.
                assert_variant!(exec.run_until_stalled(&mut scan_fut1), Poll::Pending);
                // Check that the second scan request was sent to the sme and send back results
                send_sme_scan_result(&mut exec, &mut sme_stream, &input_aps); // for output_iter_fut1
                // Process SME result.
                assert_variant!(exec.run_until_stalled(&mut scan_fut1), Poll::Pending);
                // The second iterator should have all its data
                assert_variant!(exec.run_until_stalled(&mut output_iter_fut1), Poll::Ready(result) => {
                    let results = result.expect("Failed to get next scan results").unwrap();
                    assert_eq!(results.len(), output_aps.len());
                    assert_eq!(results, output_aps);
                });

                // Send the remaining APs for the first iterator
                let mut aps = input_aps[1..].to_vec();
                ctrl.send_on_result(&mut aps.iter_mut())
                    .expect("failed to send scan data");
                // Process SME result.
                assert_variant!(exec.run_until_stalled(&mut scan_fut0), Poll::Pending);
                // Send the end of data
                ctrl.send_on_finished()
                    .expect("failed to send scan data");
            }
        );

        // Process response from SME
        assert_variant!(exec.run_until_stalled(&mut scan_fut0), Poll::Pending);

        // The first iterator should have all its data
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut0), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results.len(), output_aps.len());
            assert_eq!(results, output_aps);
        });
    }

    #[test]
    fn send_successful_scans_to_observers() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (client, mut sme_stream) = exec.run_singlethreaded(create_client());

        // Create two sets of endpoints
        let (iter0, iter_server0) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        let (iter1, iter_server1) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");

        // Issue request to scan.
        let scan_fut = perform_scan(client, iter_server0, move || vec![iter_server1]);
        pin_mut!(scan_fut);

        // Progress scan handler forward so that it will send an SME request.
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Create mock scan data and send it via the SME
        let (input_aps, output_aps) = create_scan_ap_data();
        send_sme_scan_result(&mut exec, &mut sme_stream, &input_aps);

        // Process response from SME
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Request a chunk of scan results. Progress until waiting on response from server side of
        // the iterator.
        let mut output_iter_fut0 = iter0.get_next();
        let mut output_iter_fut1 = iter1.get_next();
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut0), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut1), Poll::Pending);

        // Progress scan handler forward so that it will respond to the iterator get next request.
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Check for results
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut0), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results, output_aps);
        });
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut1), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results, output_aps);
        });

        // Request the next chunk of scan results. Progress until waiting on response from server side of
        // the iterator.
        let mut output_iter_fut0 = iter0.get_next();
        let mut output_iter_fut1 = iter1.get_next();

        // Process scan handler
        // Note: this will be Poll::Ready because the scan handler will exit after sending the final
        // scan results.
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Ready(()));

        // Check for results
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut0), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results, vec![]);
        });
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut1), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results, vec![]);
        });
    }

    #[test]
    fn scan_error_not_sent_to_observers() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (client, mut sme_stream) = exec.run_singlethreaded(create_client());

        // Create endpoint
        let (iter, iter_server) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");

        // Issue request to scan.
        // Include a panic if the observer-getting function is called
        let scan_fut = perform_scan(client, iter_server, || {
            panic!("Should not request observers on failed scan")
        });
        pin_mut!(scan_fut);

        // Request a chunk of scan results. Progress until waiting on response from server side of
        // the iterator.
        let mut output_iter_fut = iter.get_next();
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut), Poll::Pending);
        // Progress scan handler forward so that it will respond to the iterator get next request.
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Check that a scan request was sent to the sme and send back an error
        assert_variant!(
            exec.run_until_stalled(&mut sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                txn, ..
            }))) => {
                // Send failed scan response.
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_error(&mut fidl_sme::ScanError {
                    code: fidl_sme::ScanErrorCode::InternalError,
                    message: "Failed to scan".to_string()
                })
                    .expect("failed to send scan error");
            }
        );

        // Process SME result.
        // Note: this will be Poll::Ready, since the scan handler will quit after sending the error
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Ready(()));

        // the iterator should have an error on it
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results");
            assert_eq!(results, Err(fidl_policy::ScanErrorCode::GeneralError));
        });
    }

    // TODO(52700) Ignore this test until the location sensor module exists.
    #[ignore]
    #[test]
    fn get_scan_observers() {
        // We must start an executor in order to use connect_to_service() from within get_successful_scan_observers()
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        set_logger_for_test(); // since we don't call create_client() in this test
        async fn unused() {};
        exec.run_singlethreaded(unused());

        // Actual test starts here
        let observers = get_successful_scan_observers();
        assert_eq!(observers.len(), 1);
    }
}
