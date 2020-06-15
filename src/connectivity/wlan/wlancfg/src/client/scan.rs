// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Manages Scan requests for the Client Policy API.
use {
    crate::{
        client::{network_selection::ScanResultUpdate, types},
        mode_management::iface_manager::IfaceManagerApi,
        util::sme_conversion::security_from_sme_protection,
    },
    anyhow::{format_err, Error},
    fidl_fuchsia_location_sensor as fidl_location_sensor, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_component::client::connect_to_service,
    futures::{future::join, lock::Mutex, prelude::*},
    log::{debug, error, info, warn},
    std::{collections::HashMap, sync::Arc},
};

// Arbitrary count of networks (ssid/security pairs) to output per request
const OUTPUT_CHUNK_NETWORK_COUNT: usize = 5;
const SCAN_TIMEOUT_SECONDS: u8 = 10;

impl From<&fidl_sme::BssInfo> for types::Bss {
    fn from(bss: &fidl_sme::BssInfo) -> types::Bss {
        types::Bss {
            bssid: bss.bssid,
            rssi: bss.rx_dbm,
            frequency: 0,       // TODO(mnck): convert channel to freq
            timestamp_nanos: 0, // TODO(mnck): find where this comes from
        }
    }
}

/// Handles incoming scan requests by creating a new SME scan request.
/// For the output_iterator, returns scan results and/or errors.
/// On successful scan, also provides scan results to successful_scan_observer().
/// In practice, the successful_scan_observer defined in this module should be used as the
/// successful_scan_observer argument to this function. See the documentation on that function
/// to better understand its purpose.
pub(crate) async fn perform_scan<F, G, Fut>(
    iface_manager: Arc<Mutex<dyn IfaceManagerApi + Send>>,
    output_iterator: fidl::endpoints::ServerEnd<fidl_policy::ScanResultIteratorMarker>,
    successful_scan_observer: F,
    successful_scan_observer_params: G,
) where
    F: FnOnce(Vec<types::ScanResult>, G) -> Fut,
    Fut: Future<Output = ()>,
{
    let txn = {
        let mut iface_manager = iface_manager.lock().await;
        match iface_manager
            .scan(SCAN_TIMEOUT_SECONDS, fidl_fuchsia_wlan_common::ScanType::Passive)
            .await
        {
            Ok(txn) => txn,
            Err(error) => {
                error!("Failed to scan: {:?}", error);
                send_scan_error(output_iterator, fidl_policy::ScanErrorCode::GeneralError)
                    .await
                    .unwrap_or_else(|e| error!("Failed to send scan results: {}", e));
                return;
            }
        }
    };
    debug!("Sent scan request to SME successfully");
    let mut stream = txn.take_event_stream();
    let mut scanned_networks = vec![];
    while let Some(Ok(event)) = stream.next().await {
        match event {
            fidl_sme::ScanTransactionEvent::OnResult { aps: new_aps } => {
                debug!("Received scan results from SME");
                scanned_networks.extend(new_aps);
            }
            fidl_sme::ScanTransactionEvent::OnFinished {} => {
                debug!("Finished getting scan results from SME");
                let scan_results = convert_scan_info(&scanned_networks);

                // Send the results to the original requester
                let requester_fut = send_scan_results(output_iterator, &scan_results)
                    .unwrap_or_else(|e| {
                        error!("Failed to send scan results to requester: {:?}", e);
                    });

                // Send the results to all consumers of the successful scan results
                let observers_fut =
                    successful_scan_observer(scan_results.clone(), successful_scan_observer_params);

                join(requester_fut, observers_fut).await;
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

/// This function accepts scan results and distributes them to several "observers".  These
/// "observers" will use the scan results for several purposes, e.g. determining
/// network quality. As of initial implementation, there are two observers included in this
/// function: the emergency location sensor, and the Network Selector module.
pub async fn successful_scan_observer(
    scan_results: Vec<types::ScanResult>,
    network_selector_updater: Arc<impl ScanResultUpdate>,
) {
    /// The location sensor module uses scan results to help determine the
    /// device's location, for use by the Emergency Location Provider.
    async fn send_to_location_sensor(scan_results: &Vec<types::ScanResult>) -> Result<(), Error> {
        // Get an output iterator
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

        // Send results to the iterator
        send_scan_results(server, scan_results).await
    }

    // Filter out any errors and just log a message.
    // No error recovery, we'll just try again next time a scan result comes in.
    if let Err(e) = send_to_location_sensor(&scan_results).await {
        warn!("Failed to send scan results to location sensor: {:?}", e)
    } else {
        debug!("Updated location sensor")
    };

    // Send to the network selector
    network_selector_updater.update_scan_results(scan_results);
}

/// Converts array of fidl_sme::BssInfo to array of internal ScanResult.
/// There is one BssInfo per BSSID. In contrast, there is one ScanResult per
/// SSID, with information for multiple BSSs contained within it.
fn convert_scan_info(scanned_networks: &[fidl_sme::BssInfo]) -> Vec<types::ScanResult> {
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
            debug!("Unknown security type present in scan results: {:?}", bss.protection);
        }
    }
    let mut scan_results: Vec<types::ScanResult> = bss_by_network
        .iter()
        .map(|(network, bss_infos)| types::ScanResult {
            id: network.clone(),
            entries: bss_infos.iter().map(types::Bss::from).collect(),
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
    scan_results: &Vec<types::ScanResult>,
) -> Result<(), Error> {
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
            return Err(format_err!("Peer closed channel for getting scan results unexpectedly"));
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            access_point::state_machine as ap_fsm, client::state_machine as client_fsm,
            config_management::Credential, util::logger::set_logger_for_test,
        },
        anyhow::Error,
        async_trait::async_trait,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_wlan_common as fidl_common, fuchsia_async as fasync,
        futures::{channel::oneshot, lock::Mutex, task::Poll},
        pin_utils::pin_mut,
        std::sync::Arc,
        wlan_common::assert_variant,
    };

    /// convert from policy fidl Credential to sme fidl Credential
    pub fn sme_credential_from_policy(cred: &Credential) -> fidl_sme::Credential {
        match cred {
            Credential::Password(pwd) => fidl_sme::Credential::Password(pwd.clone()),
            Credential::Psk(psk) => fidl_sme::Credential::Psk(psk.clone()),
            Credential::None => fidl_sme::Credential::None(fidl_sme::Empty {}),
        }
    }

    struct FakeIfaceManager {
        pub sme_proxy: fidl_fuchsia_wlan_sme::ClientSmeProxy,
        pub connect_response: Result<(), ()>,
        pub client_connections_enabled: bool,
    }

    impl FakeIfaceManager {
        pub fn new(proxy: fidl_fuchsia_wlan_sme::ClientSmeProxy) -> Self {
            FakeIfaceManager {
                sme_proxy: proxy,
                connect_response: Ok(()),
                client_connections_enabled: false,
            }
        }
    }

    #[async_trait]
    impl IfaceManagerApi for FakeIfaceManager {
        async fn disconnect(
            &mut self,
            _network_id: fidl_fuchsia_wlan_policy::NetworkIdentifier,
        ) -> Result<(), Error> {
            Ok(())
        }

        async fn connect(
            &mut self,
            connect_req: client_fsm::ConnectRequest,
        ) -> Result<oneshot::Receiver<()>, Error> {
            let credential = sme_credential_from_policy(&connect_req.credential);
            let mut req = fidl_sme::ConnectRequest {
                ssid: connect_req.network.ssid,
                credential,
                radio_cfg: fidl_sme::RadioConfig {
                    override_phy: false,
                    phy: fidl_common::Phy::Ht,
                    override_cbw: false,
                    cbw: fidl_common::Cbw::Cbw20,
                    override_primary_chan: false,
                    primary_chan: 0,
                },
                deprecated_scan_type: fidl_common::ScanType::Passive,
            };
            self.sme_proxy.connect(&mut req, None)?;

            let (responder, receiver) = oneshot::channel();
            let _ = responder.send(());
            Ok(receiver)
        }

        fn record_idle_client(&mut self, _iface_id: u16) {}

        fn has_idle_client(&self) -> bool {
            true
        }

        async fn scan(
            &mut self,
            timeout: u8,
            scan_type: fidl_fuchsia_wlan_common::ScanType,
        ) -> Result<fidl_fuchsia_wlan_sme::ScanTransactionProxy, Error> {
            let (local, remote) = fidl::endpoints::create_proxy()?;
            let mut request =
                fidl_fuchsia_wlan_sme::ScanRequest { timeout: timeout, scan_type: scan_type };
            let _ = self.sme_proxy.scan(&mut request, remote);
            Ok(local)
        }

        async fn stop_client_connections(&mut self) -> Result<(), Error> {
            self.client_connections_enabled = false;
            Ok(())
        }

        async fn start_client_connections(&mut self) -> Result<(), Error> {
            self.client_connections_enabled = true;
            Ok(())
        }

        async fn start_ap(
            &mut self,
            _config: ap_fsm::ApConfig,
        ) -> Result<oneshot::Receiver<fidl_fuchsia_wlan_sme::StartApResultCode>, Error> {
            let (_, receiver) = oneshot::channel();
            Ok(receiver)
        }

        async fn stop_ap(&mut self, _ssid: Vec<u8>, _password: Vec<u8>) -> Result<(), Error> {
            Ok(())
        }

        async fn stop_all_aps(&mut self) -> Result<(), Error> {
            Ok(())
        }
    }

    /// Creates a Client wrapper.
    async fn create_iface_manager(
    ) -> (Arc<Mutex<FakeIfaceManager>>, fidl_sme::ClientSmeRequestStream) {
        set_logger_for_test();
        let (client_sme, remote) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("error creating proxy");
        let iface_manager = Arc::new(Mutex::new(FakeIfaceManager::new(client_sme)));
        (iface_manager, remote.into_stream().expect("failed to create stream"))
    }

    struct MockNetworkSelection {}
    impl ScanResultUpdate for MockNetworkSelection {
        // No-op scan result updater
        fn update_scan_results(&self, _scan_results: Vec<types::ScanResult>) {}
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
        let (client, mut sme_stream) = exec.run_singlethreaded(create_iface_manager());

        // Issue request to scan.
        let (iter, iter_server) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        let scan_fut = perform_scan(client, iter_server, |_, _| async {}, MockNetworkSelection {});
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
        let (client, mut sme_stream) = exec.run_singlethreaded(create_iface_manager());

        // Issue request to scan.
        let (_iter, iter_server) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        let scan_fut =
            perform_scan(client.clone(), iter_server, |_, _| async {}, MockNetworkSelection {});
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
        let scan_fut2 =
            perform_scan(client, iter_server2, |_, _| async {}, MockNetworkSelection {});
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
        let (client, mut sme_stream) = exec.run_singlethreaded(create_iface_manager());

        // Issue request to scan.
        let (iter, iter_server) =
            fidl::endpoints::create_endpoints().expect("failed to create iterator");
        let scan_fut = perform_scan(client, iter_server, |_, _| async {}, MockNetworkSelection {});
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
        let (client, mut sme_stream) = exec.run_singlethreaded(create_iface_manager());

        // Issue request to scan.
        let (iter, iter_server) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        let scan_fut = perform_scan(client, iter_server, |_, _| async {}, MockNetworkSelection {});
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
        let (client, mut sme_stream) = exec.run_singlethreaded(create_iface_manager());
        let (input_aps, output_aps) = create_scan_ap_data();

        // Create two sets of endpoints
        let (iter0, iter_server0) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        let (iter1, iter_server1) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");

        // Issue request to scan on both iterator.
        let scan_fut0 =
            perform_scan(client.clone(), iter_server0, |_, _| async {}, MockNetworkSelection {});
        pin_mut!(scan_fut0);
        let scan_fut1 =
            perform_scan(client, iter_server1, |_, _| async {}, MockNetworkSelection {});
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
        let (client, mut sme_stream) = exec.run_singlethreaded(create_iface_manager());

        // Create two sets of endpoints
        let (iter0, iter_server0) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        let (iter1, iter_server1) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");

        // Issue request to scan.
        let scan_fut = perform_scan(
            client,
            iter_server0,
            move |scan_results, iter_server| async move {
                send_scan_results(iter_server, &scan_results).await.unwrap();
            },
            iter_server1,
        );
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
        let (client, mut sme_stream) = exec.run_singlethreaded(create_iface_manager());

        // Create endpoint
        let (iter, iter_server) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");

        // Issue request to scan.
        // Include a panic if the observer function is called
        let scan_fut = perform_scan(
            client,
            iter_server,
            |_, _| async { panic!("Should not request observers on failed scan") },
            (),
        );
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
    fn scan_observer_sends_to_location_sensor() {
        set_logger_for_test();
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (aps, _) = create_scan_ap_data();
        let fut =
            successful_scan_observer(convert_scan_info(&aps), Arc::new(MockNetworkSelection {}));
        let _result = exec.run_singlethreaded(fut);
        panic!("Need to reach into location sensor and check it got data")
    }

    #[test]
    fn scan_observer_sends_to_network_selector() {
        set_logger_for_test();
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        use std::sync::Mutex;
        struct TestNetworkSelection {
            called: Mutex<bool>,
        }
        impl ScanResultUpdate for TestNetworkSelection {
            fn update_scan_results(&self, scan_results: Vec<types::ScanResult>) {
                let (aps, _) = create_scan_ap_data();
                assert_eq!(convert_scan_info(&aps), scan_results);
                let mut guard = self.called.lock().unwrap();
                *guard = true;
            }
        }

        let (aps, _) = create_scan_ap_data();
        let fut = successful_scan_observer(
            convert_scan_info(&aps),
            Arc::new(TestNetworkSelection { called: Mutex::new(false) }),
        );
        let result = exec.run_singlethreaded(fut);
        assert_variant!(result, ());
    }
}
