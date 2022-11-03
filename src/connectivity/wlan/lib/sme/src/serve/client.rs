// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    client::{
        self as client_sme, ConnectResult, ConnectTransactionEvent, ConnectTransactionStream,
    },
    MlmeEventStream, MlmeSink, MlmeStream,
};
use fidl::{endpoints::RequestStream, endpoints::ServerEnd};
use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211;
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use fidl_fuchsia_wlan_sme::{self as fidl_sme, ClientSmeRequest, TelemetryRequest};
use fuchsia_inspect_contrib::auto_persist;
use fuchsia_zircon as zx;
use futures::channel::mpsc;
use futures::{prelude::*, select};
use log::error;
use pin_utils::pin_mut;
use std::sync::{Arc, Mutex};
use wlan_common::hasher::WlanHasher;
use wlan_inspect;

pub type Endpoint = ServerEnd<fidl_sme::ClientSmeMarker>;
type Sme = client_sme::ClientSme;

pub fn serve(
    cfg: crate::Config,
    device_info: fidl_mlme::DeviceInfo,
    mac_sublayer_support: fidl_common::MacSublayerSupport,
    security_support: fidl_common::SecuritySupport,
    spectrum_management_support: fidl_common::SpectrumManagementSupport,
    event_stream: MlmeEventStream,
    new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
    new_telemetry_fidl_clients: mpsc::UnboundedReceiver<
        fidl::endpoints::ServerEnd<fidl_sme::TelemetryMarker>,
    >,
    iface_tree_holder: Arc<wlan_inspect::iface_mgr::IfaceTreeHolder>,
    hasher: WlanHasher,
    persistence_req_sender: auto_persist::PersistenceReqSender,
) -> (MlmeSink, MlmeStream, impl Future<Output = Result<(), anyhow::Error>>) {
    let wpa3_supported = security_support.mfp.supported
        && (security_support.sae.driver_handler_supported
            || security_support.sae.sme_handler_supported);
    let cfg = client_sme::ClientConfig::from_config(cfg, wpa3_supported);
    let (sme, mlme_sink, mlme_stream, time_stream) = Sme::new(
        cfg,
        device_info,
        iface_tree_holder,
        hasher,
        persistence_req_sender,
        mac_sublayer_support,
        security_support,
        spectrum_management_support,
    );
    let fut = async move {
        let sme = Arc::new(Mutex::new(sme));
        let mlme_sme = super::serve_mlme_sme(event_stream, Arc::clone(&sme), time_stream);
        let sme_fidl = super::serve_fidl(&*sme, new_fidl_clients, handle_fidl_request);
        let telemetry_fidl =
            super::serve_fidl(&*sme, new_telemetry_fidl_clients, handle_telemetry_fidl_request);
        pin_mut!(mlme_sme);
        pin_mut!(sme_fidl);
        select! {
            mlme_sme = mlme_sme.fuse() => mlme_sme?,
            sme_fidl = sme_fidl.fuse() => match sme_fidl? {},
            telemetry_fidl = telemetry_fidl.fuse() => match telemetry_fidl? {},
        }
        Ok(())
    };
    (mlme_sink, mlme_stream, fut)
}

async fn handle_fidl_request(
    sme: &Mutex<Sme>,
    request: fidl_sme::ClientSmeRequest,
) -> Result<(), fidl::Error> {
    match request {
        ClientSmeRequest::Scan { req, responder } => Ok(scan(sme, req, responder)
            .await
            .unwrap_or_else(|e| error!("Error handling a scan transaction: {:?}", e))),
        ClientSmeRequest::Connect { req, txn, .. } => Ok(connect(sme, txn, req)
            .await
            .unwrap_or_else(|e| error!("Error handling a connect transaction: {:?}", e))),
        ClientSmeRequest::Disconnect { responder, reason } => {
            disconnect(sme, reason);
            responder.send()
        }
        ClientSmeRequest::Status { responder } => responder.send(&mut status(sme)),
        ClientSmeRequest::WmmStatus { responder } => wmm_status(sme, responder).await,
    }
}

async fn handle_telemetry_fidl_request(
    sme: &Mutex<Sme>,
    request: TelemetryRequest,
) -> Result<(), fidl::Error> {
    match request {
        TelemetryRequest::GetCounterStats { responder, .. } => {
            let counter_stats_fut = sme.lock().unwrap().counter_stats();
            let mut counter_stats = counter_stats_fut
                .await
                .map_err(|_| zx::Status::CONNECTION_ABORTED.into_raw())
                .and_then(|stats| match stats {
                    fidl_mlme::GetIfaceCounterStatsResponse::Stats(stats) => Ok(stats),
                    fidl_mlme::GetIfaceCounterStatsResponse::ErrorStatus(err) => Err(err),
                });
            responder.send(&mut counter_stats)
        }
        TelemetryRequest::GetHistogramStats { responder, .. } => {
            let histogram_stats_fut = sme.lock().unwrap().histogram_stats();
            let mut histogram_stats = histogram_stats_fut
                .await
                .map_err(|_| zx::Status::CONNECTION_ABORTED.into_raw())
                .and_then(|stats| match stats {
                    fidl_mlme::GetIfaceHistogramStatsResponse::Stats(stats) => Ok(stats),
                    fidl_mlme::GetIfaceHistogramStatsResponse::ErrorStatus(err) => Err(err),
                });
            responder.send(&mut histogram_stats)
        }
    }
}

async fn scan(
    sme: &Mutex<Sme>,
    request: fidl_sme::ScanRequest,
    responder: fidl_sme::ClientSmeScanResponder,
) -> Result<(), anyhow::Error> {
    let receiver = sme.lock().unwrap().on_scan_command(request);
    let receive_result = match receiver.await {
        Ok(receive_result) => receive_result,
        Err(e) => {
            error!("Scan receiver error: {:?}", e);
            return filter_out_peer_closed(
                responder.send(&mut Err(fidl_sme::ScanErrorCode::InternalError)),
            )
            .map_err(anyhow::Error::from);
        }
    };

    let send_result = match receive_result {
        Ok(scan_results) => responder.send(&mut Ok(scan_results
            .into_iter()
            .map(fidl_sme::ScanResult::from)
            .collect::<Vec<_>>())),
        Err(mlme_scan_result_code) => {
            let scan_error_code = match mlme_scan_result_code {
                fidl_mlme::ScanResultCode::Success | fidl_mlme::ScanResultCode::InvalidArgs => {
                    error!("Internal scan error: {:?}", mlme_scan_result_code);
                    fidl_sme::ScanErrorCode::InternalError
                }
                fidl_mlme::ScanResultCode::NotSupported => fidl_sme::ScanErrorCode::NotSupported,
                fidl_mlme::ScanResultCode::InternalError => {
                    fidl_sme::ScanErrorCode::InternalMlmeError
                }
                fidl_mlme::ScanResultCode::ShouldWait => fidl_sme::ScanErrorCode::ShouldWait,
                fidl_mlme::ScanResultCode::CanceledByDriverOrFirmware => {
                    fidl_sme::ScanErrorCode::CanceledByDriverOrFirmware
                }
            };
            responder.send(&mut Err(scan_error_code))
        }
    };
    filter_out_peer_closed(send_result).map_err(anyhow::Error::from)
}

async fn connect(
    sme: &Mutex<Sme>,
    txn: Option<ServerEnd<fidl_sme::ConnectTransactionMarker>>,
    req: fidl_sme::ConnectRequest,
) -> Result<(), anyhow::Error> {
    let handle = match txn {
        None => None,
        Some(txn) => Some(txn.into_stream()?.control_handle()),
    };
    let connect_txn_stream = sme.lock().unwrap().on_connect_command(req);
    serve_connect_txn_stream(handle, connect_txn_stream).await?;
    Ok(())
}

async fn serve_connect_txn_stream(
    handle: Option<fidl_sme::ConnectTransactionControlHandle>,
    mut connect_txn_stream: ConnectTransactionStream,
) -> Result<(), anyhow::Error> {
    if let Some(handle) = handle {
        loop {
            match connect_txn_stream.next().fuse().await {
                Some(event) => filter_out_peer_closed(match event {
                    ConnectTransactionEvent::OnConnectResult { result, is_reconnect } => {
                        let mut connect_result = convert_connect_result(&result, is_reconnect);
                        handle.send_on_connect_result(&mut connect_result)
                    }
                    ConnectTransactionEvent::OnDisconnect { mut info } => {
                        handle.send_on_disconnect(&mut info)
                    }
                    ConnectTransactionEvent::OnSignalReport { mut ind } => {
                        handle.send_on_signal_report(&mut ind)
                    }
                    ConnectTransactionEvent::OnChannelSwitched { mut info } => {
                        handle.send_on_channel_switched(&mut info)
                    }
                })?,
                // SME has dropped the ConnectTransaction endpoint, likely due to a disconnect.
                None => return Ok(()),
            }
        }
    }
    Ok(())
}

pub fn filter_out_peer_closed(r: Result<(), fidl::Error>) -> Result<(), fidl::Error> {
    match r {
        Err(ref e) if e.is_closed() => Ok(()),
        other => other,
    }
}

fn disconnect(sme: &Mutex<Sme>, policy_disconnect_reason: fidl_sme::UserDisconnectReason) {
    sme.lock().unwrap().on_disconnect_command(policy_disconnect_reason);
}

fn status(sme: &Mutex<Sme>) -> fidl_sme::ClientStatusResponse {
    sme.lock().unwrap().status().into()
}

async fn wmm_status(
    sme: &Mutex<Sme>,
    responder: fidl_sme::ClientSmeWmmStatusResponder,
) -> Result<(), fidl::Error> {
    let receiver = sme.lock().unwrap().wmm_status();
    let mut wmm_status = match receiver.await {
        Ok(result) => result,
        Err(_) => Err(zx::sys::ZX_ERR_CANCELED),
    };
    responder.send(&mut wmm_status)
}

fn convert_connect_result(result: &ConnectResult, is_reconnect: bool) -> fidl_sme::ConnectResult {
    let (code, is_credential_rejected) = match result {
        ConnectResult::Success => (fidl_ieee80211::StatusCode::Success, false),
        ConnectResult::Canceled => (fidl_ieee80211::StatusCode::Canceled, false),
        ConnectResult::Failed(failure) => {
            (failure.status_code(), failure.likely_due_to_credential_rejected())
        }
    };
    fidl_sme::ConnectResult { code, is_credential_rejected, is_reconnect }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::client::{
            ConnectFailure, ConnectResult, EstablishRsnaFailure, EstablishRsnaFailureReason,
        },
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_wlan_internal as fidl_internal,
        fidl_fuchsia_wlan_mlme::ScanResultCode,
        fidl_fuchsia_wlan_sme::{self as fidl_sme},
        fuchsia_async as fasync,
        futures::{stream::StreamFuture, task::Poll},
        pin_utils::pin_mut,
        rand::{prelude::ThreadRng, Rng},
        test_case::test_case,
        wlan_common::{assert_variant, random_bss_description},
        wlan_rsn::auth,
    };

    #[test]
    fn test_convert_connect_result() {
        assert_eq!(
            convert_connect_result(&ConnectResult::Success, false),
            fidl_sme::ConnectResult {
                code: fidl_ieee80211::StatusCode::Success,
                is_credential_rejected: false,
                is_reconnect: false,
            }
        );
        assert_eq!(
            convert_connect_result(&ConnectResult::Canceled, true),
            fidl_sme::ConnectResult {
                code: fidl_ieee80211::StatusCode::Canceled,
                is_credential_rejected: false,
                is_reconnect: true,
            }
        );
        let connect_result =
            ConnectResult::Failed(ConnectFailure::ScanFailure(ScanResultCode::ShouldWait));
        assert_eq!(
            convert_connect_result(&connect_result, false),
            fidl_sme::ConnectResult {
                code: fidl_ieee80211::StatusCode::Canceled,
                is_credential_rejected: false,
                is_reconnect: false,
            }
        );

        let connect_result =
            ConnectResult::Failed(ConnectFailure::EstablishRsnaFailure(EstablishRsnaFailure {
                auth_method: Some(auth::MethodName::Psk),
                reason: EstablishRsnaFailureReason::InternalError,
            }));
        assert_eq!(
            convert_connect_result(&connect_result, false),
            fidl_sme::ConnectResult {
                code: fidl_ieee80211::StatusCode::EstablishRsnaFailure,
                is_credential_rejected: false,
                is_reconnect: false,
            }
        );

        let connect_result =
            ConnectResult::Failed(ConnectFailure::EstablishRsnaFailure(EstablishRsnaFailure {
                auth_method: Some(auth::MethodName::Psk),
                reason: EstablishRsnaFailureReason::RsnaResponseTimeout(
                    wlan_rsn::Error::LikelyWrongCredential,
                ),
            }));
        assert_eq!(
            convert_connect_result(&connect_result, false),
            fidl_sme::ConnectResult {
                code: fidl_ieee80211::StatusCode::EstablishRsnaFailure,
                is_credential_rejected: true,
                is_reconnect: false,
            }
        );

        let connect_result =
            ConnectResult::Failed(ConnectFailure::EstablishRsnaFailure(EstablishRsnaFailure {
                auth_method: Some(auth::MethodName::Psk),
                reason: EstablishRsnaFailureReason::RsnaCompletionTimeout(
                    wlan_rsn::Error::LikelyWrongCredential,
                ),
            }));
        assert_eq!(
            convert_connect_result(&connect_result, false),
            fidl_sme::ConnectResult {
                code: fidl_ieee80211::StatusCode::EstablishRsnaFailure,
                is_credential_rejected: true,
                is_reconnect: false,
            }
        );

        let connect_result =
            ConnectResult::Failed(ConnectFailure::EstablishRsnaFailure(EstablishRsnaFailure {
                auth_method: Some(auth::MethodName::Psk),
                reason: EstablishRsnaFailureReason::RsnaCompletionTimeout(
                    wlan_rsn::Error::MissingGtkProvider,
                ),
            }));
        assert_eq!(
            convert_connect_result(&connect_result, false),
            fidl_sme::ConnectResult {
                code: fidl_ieee80211::StatusCode::EstablishRsnaFailure,
                is_credential_rejected: false,
                is_reconnect: false,
            }
        );

        let connect_result =
            ConnectResult::Failed(ConnectFailure::ScanFailure(ScanResultCode::InternalError));
        assert_eq!(
            convert_connect_result(&connect_result, false),
            fidl_sme::ConnectResult {
                code: fidl_ieee80211::StatusCode::RefusedReasonUnspecified,
                is_credential_rejected: false,
                is_reconnect: false,
            }
        );
    }

    // TODO(fxbug.dev/83885): There is no test coverage for consistency between MLME scan results
    // and SME scan results produced by wlanstack. In particular, the timestamp_nanos field
    // of fidl_mlme::ScanResult is dropped in SME, and no tests reveal this problem.

    #[test_case(1, true; "with 1 result")]
    #[test_case(2, true; "with 2 results")]
    #[test_case(30, true; "with 30 results")]
    #[test_case(4000, true; "with 4000 results")]
    #[test_case(50000, false; "with 50000 results")]
    #[test_case(100000, false; "with 100000 results")]
    fn scan_results_are_effectively_unbounded(number_of_scan_results: usize, randomize: bool) {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (client_sme_proxy, mut client_sme_stream) =
            create_proxy_and_stream::<fidl_sme::ClientSmeMarker>().expect("error creating proxy");

        // Request scan
        async fn request_and_collect_result(
            client_sme_proxy: &fidl_sme::ClientSmeProxy,
        ) -> fidl_sme::ClientSmeScanResult {
            client_sme_proxy
                .scan(&mut fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {}))
                .await
                .expect("FIDL request failed")
        }

        let result_fut = request_and_collect_result(&client_sme_proxy);
        pin_mut!(result_fut);

        assert_variant!(exec.run_until_stalled(&mut result_fut), Poll::Pending);

        // Generate and send scan results
        let mut rng = rand::thread_rng();
        let scan_result_list = if randomize {
            (0..number_of_scan_results).map(|_| random_scan_result(&mut rng).into()).collect()
        } else {
            vec![random_scan_result(&mut rng).into(); number_of_scan_results]
        };
        assert_variant!(exec.run_until_stalled(&mut client_sme_stream.next()),
                        Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                            req: _, responder,
                        }))) => {
                            responder.send(&mut Ok(scan_result_list.clone())).expect("failed to send scan results");
                        }
        );

        // Verify scan results
        assert_variant!(exec.run_until_stalled(&mut result_fut), Poll::Ready(Ok(received_scan_result_list)) => {
            assert_eq!(scan_result_list, received_scan_result_list);
        })
    }

    #[test]
    fn test_serve_connect_txn_stream() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");

        let (sme_proxy, sme_connect_txn_stream) = mpsc::unbounded();
        let (fidl_client_proxy, fidl_connect_txn_stream) =
            create_proxy_and_stream::<fidl_sme::ConnectTransactionMarker>()
                .expect("failed to create ConnectTransaction proxy and stream");
        let fidl_client_fut = fidl_client_proxy.take_event_stream().into_future();
        pin_mut!(fidl_client_fut);
        let fidl_connect_txn_handle = fidl_connect_txn_stream.control_handle();

        let test_fut =
            serve_connect_txn_stream(Some(fidl_connect_txn_handle), sme_connect_txn_stream);
        pin_mut!(test_fut);

        // Test sending OnConnectResult
        sme_proxy
            .unbounded_send(ConnectTransactionEvent::OnConnectResult {
                result: ConnectResult::Success,
                is_reconnect: true,
            })
            .expect("expect sending ConnectTransactionEvent to succeed");
        assert_variant!(exec.run_until_stalled(&mut test_fut), Poll::Pending);
        let event = assert_variant!(poll_stream_fut(&mut exec, &mut fidl_client_fut), Poll::Ready(Some(Ok(event))) => event);
        assert_variant!(
            event,
            fidl_sme::ConnectTransactionEvent::OnConnectResult {
                result: fidl_sme::ConnectResult {
                    code: fidl_ieee80211::StatusCode::Success,
                    is_credential_rejected: false,
                    is_reconnect: true,
                }
            }
        );

        // Test sending OnDisconnect
        let input_info = fidl_sme::DisconnectInfo {
            is_sme_reconnecting: true,
            disconnect_source: fidl_sme::DisconnectSource::Mlme(fidl_sme::DisconnectCause {
                reason_code: fidl_ieee80211::ReasonCode::UnspecifiedReason,
                mlme_event_name: fidl_sme::DisconnectMlmeEventName::DeauthenticateIndication,
            }),
        };
        sme_proxy
            .unbounded_send(ConnectTransactionEvent::OnDisconnect { info: input_info.clone() })
            .expect("expect sending ConnectTransactionEvent to succeed");
        assert_variant!(exec.run_until_stalled(&mut test_fut), Poll::Pending);
        let event = assert_variant!(poll_stream_fut(&mut exec, &mut fidl_client_fut), Poll::Ready(Some(Ok(event))) => event);
        assert_variant!(event, fidl_sme::ConnectTransactionEvent::OnDisconnect { info: output_info } => {
            assert_eq!(input_info, output_info);
        });

        // Test sending OnSignalReport
        let input_ind = fidl_internal::SignalReportIndication { rssi_dbm: -40, snr_db: 30 };
        sme_proxy
            .unbounded_send(ConnectTransactionEvent::OnSignalReport { ind: input_ind.clone() })
            .expect("expect sending ConnectTransactionEvent to succeed");
        assert_variant!(exec.run_until_stalled(&mut test_fut), Poll::Pending);
        let event = assert_variant!(poll_stream_fut(&mut exec, &mut fidl_client_fut), Poll::Ready(Some(Ok(event))) => event);
        assert_variant!(event, fidl_sme::ConnectTransactionEvent::OnSignalReport { ind } => {
            assert_eq!(input_ind, ind);
        });

        // Test sending OnChannelSwitched
        let input_info = fidl_internal::ChannelSwitchInfo { new_channel: 8 };
        sme_proxy
            .unbounded_send(ConnectTransactionEvent::OnChannelSwitched { info: input_info.clone() })
            .expect("expect sending ConnectTransactionEvent to succeed");
        assert_variant!(exec.run_until_stalled(&mut test_fut), Poll::Pending);
        let event = assert_variant!(poll_stream_fut(&mut exec, &mut fidl_client_fut), Poll::Ready(Some(Ok(event))) => event);
        assert_variant!(event, fidl_sme::ConnectTransactionEvent::OnChannelSwitched { info } => {
            assert_eq!(input_info, info);
        });

        // When SME proxy is dropped, the fut should terminate
        std::mem::drop(sme_proxy);
        assert_variant!(exec.run_until_stalled(&mut test_fut), Poll::Ready(Ok(())));
    }

    fn poll_stream_fut<S: Stream + std::marker::Unpin>(
        exec: &mut fasync::TestExecutor,
        stream_fut: &mut StreamFuture<S>,
    ) -> Poll<Option<S::Item>> {
        exec.run_until_stalled(stream_fut).map(|(item, stream)| {
            *stream_fut = stream.into_future();
            item
        })
    }

    // Create roughly over 2k bytes ScanResult
    fn random_scan_result(rng: &mut ThreadRng) -> wlan_common::scan::ScanResult {
        use wlan_common::security::SecurityDescriptor;

        // TODO(fxbug.dev/83740): Merge this with a similar function in wlancfg.
        wlan_common::scan::ScanResult {
            compatibility: match rng.gen_range(0..4) {
                0 => wlan_common::scan::Compatibility::expect_some([SecurityDescriptor::OPEN]),
                1 => wlan_common::scan::Compatibility::expect_some([
                    SecurityDescriptor::WPA2_PERSONAL,
                ]),
                2 => wlan_common::scan::Compatibility::expect_some([
                    SecurityDescriptor::WPA2_PERSONAL,
                    SecurityDescriptor::WPA3_PERSONAL,
                ]),
                _ => None,
            },
            timestamp: zx::Time::from_nanos(rng.gen()),
            bss_description: random_bss_description!(),
        }
    }
}
