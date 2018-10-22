// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::bail;
use fidl::{endpoints::RequestStream, endpoints::ServerEnd};
use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEventStream, MlmeProxy};
use fidl_fuchsia_wlan_sme::{self as fidl_sme, ClientSmeRequest};
use futures::{prelude::*, select};
use futures::channel::mpsc;
use log::{error, info};
use pin_utils::pin_mut;
use std::marker::Unpin;
use std::sync::{Arc, Mutex};
use wlan_sme::{client as client_sme, DeviceInfo, InfoStream};
use wlan_sme::client::{BssInfo, ConnectionAttemptId, ConnectResult,
                       ConnectPhyParams, DiscoveryError,
                       EssDiscoveryResult, EssInfo, InfoEvent, ScanTxnId};
use fuchsia_zircon as zx;

use crate::cobalt_reporter::CobaltSender;
use crate::fidl_util::is_peer_closed;
use crate::future_util::ConcurrentTasks;
use crate::Never;
use crate::stats_scheduler::StatsRequest;
use crate::telemetry;

struct Tokens;

impl client_sme::Tokens for Tokens {
    type ScanToken = fidl_sme::ScanTransactionControlHandle;
    type ConnectToken = Option<fidl_sme::ConnectTransactionControlHandle>;
}

pub type Endpoint = ServerEnd<fidl_sme::ClientSmeMarker>;
type Sme = client_sme::ClientSme<Tokens>;

struct ConnectionTimes {
    att_id: ConnectionAttemptId,
    txn_id: ScanTxnId,
    connect_started_time: Option<zx::Time>,
    scan_started_time: Option<zx::Time>,
    assoc_started_time: Option<zx::Time>,
    rsna_started_time: Option<zx::Time>,
}

pub async fn serve<S>(proxy: MlmeProxy,
                      device_info: DeviceInfo,
                      event_stream: MlmeEventStream,
                      new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
                      stats_requests: S,
                      cobalt_sender: CobaltSender)
    -> Result<(), failure::Error>
    where S: Stream<Item = StatsRequest> + Unpin
{
    let (sme, mlme_stream, user_stream, info_stream) = Sme::new(device_info);
    let sme = Arc::new(Mutex::new(sme));
    let mlme_sme = super::serve_mlme_sme(
        proxy, event_stream, Arc::clone(&sme), mlme_stream, stats_requests);
    let sme_fidl = serve_fidl(sme, new_fidl_clients, user_stream, info_stream, cobalt_sender);
    pin_mut!(mlme_sme);
    pin_mut!(sme_fidl);
    Ok(select! {
        mlme_sme => mlme_sme?,
        sme_fidl => sme_fidl?.into_any(),
    })
}

async fn serve_fidl(sme: Arc<Mutex<Sme>>,
                    mut new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
                    mut user_stream: client_sme::UserStream<Tokens>,
                    mut info_stream: InfoStream,
                    mut cobalt_sender: CobaltSender)
    -> Result<Never, failure::Error>
{
    let mut fidl_clients = ConcurrentTasks::new();
    let mut connection_times = ConnectionTimes { att_id: 0,
                                                 txn_id: 0,
                                                 connect_started_time: None,
                                                 scan_started_time: None,
                                                 assoc_started_time: None,
                                                 rsna_started_time: None };
    loop {
        let mut user_event = user_stream.next();
        let mut info_event = info_stream.next();
        let mut new_fidl_client = new_fidl_clients.next();
        select! {
            user_event => match user_event {
                Some(e) => handle_user_event(e),
                None => bail!("SME->FIDL future unexpectedly finished"),
            },
            info_event => match info_event {
                Some(e) => handle_info_event(e, &mut cobalt_sender, &mut connection_times),
                None => bail!("Info Event stream unexpectedly ended"),
            },
            fidl_clients => fidl_clients.into_any(),
            new_fidl_client => match new_fidl_client {
                Some(c) => fidl_clients.add(serve_fidl_endpoint(Arc::clone(&sme), c)),
                None => bail!("New FIDL client stream unexpectedly ended"),
            },
        }
    }
}

async fn serve_fidl_endpoint(sme: Arc<Mutex<Sme>>, endpoint: Endpoint) {
    let mut stream = match endpoint.into_stream() {
        Ok(s) => s,
        Err(e) => {
            error!("Failed to create a stream from a zircon channel: {}", e);
            return;
        }
    };
    while let Some(request) = await!(stream.next()) {
        let request = match request {
            Ok(r) => r,
            Err(e) => {
                error!("Error reading a FIDL request from a channel: {}", e);
                return;
            }
        };
        if let Err(e) = handle_fidl_request(&sme, request) {
            error!("Error serving a FIDL request: {}", e);
            return;
        }
    }
}

fn handle_fidl_request(sme: &Arc<Mutex<Sme>>, request: fidl_sme::ClientSmeRequest)
    -> Result<(), fidl::Error>
{
    match request {
        ClientSmeRequest::Scan { txn, .. } => {
            Ok(scan(sme, txn)
                .unwrap_or_else(|e| error!("Error starting a scan transaction: {:?}", e)))
        },
        ClientSmeRequest::Connect { req, txn, .. } => {
            Ok(connect(sme, req.ssid, req.password, txn, req.params)
                .unwrap_or_else(|e| error!("Error starting a connect transaction: {:?}", e)))
        },
        ClientSmeRequest::Disconnect { responder } => {
            disconnect(sme);
            responder.send()
        }
        ClientSmeRequest::Status { responder } => responder.send(&mut status(&sme)),
    }
}

fn scan(sme: &Arc<Mutex<Sme>>,
        txn: ServerEnd<fidl_sme::ScanTransactionMarker>)
    -> Result<(), failure::Error>
{
    let handle = txn.into_stream()?.control_handle();
    sme.lock().unwrap().on_scan_command(handle);
    Ok(())
}


fn convert_to_mlme_phy(sme_phy: fidl_sme::Phy) -> fidl_mlme::Phy {
    match sme_phy {
        fidl_sme::Phy::Hr => fidl_mlme::Phy::Hr,
        fidl_sme::Phy::Erp => fidl_mlme::Phy::Erp,
        fidl_sme::Phy::Ht => fidl_mlme::Phy::Ht,
        fidl_sme::Phy::Vht => fidl_mlme::Phy::Vht,
        fidl_sme::Phy::Hew => fidl_mlme::Phy::Hew,
    }
}

fn convert_to_mlme_cbw(sme_cbw: fidl_sme::Cbw) -> fidl_mlme::Cbw {
    match sme_cbw {
        fidl_sme::Cbw::Cbw20 => fidl_mlme::Cbw::Cbw20,
        fidl_sme::Cbw::Cbw40 => fidl_mlme::Cbw::Cbw40,
        fidl_sme::Cbw::Cbw40Below => fidl_mlme::Cbw::Cbw40Below,
        fidl_sme::Cbw::Cbw80 => fidl_mlme::Cbw::Cbw80,
        fidl_sme::Cbw::Cbw160 => fidl_mlme::Cbw::Cbw160,
        fidl_sme::Cbw::Cbw80P80 => fidl_mlme::Cbw::Cbw80P80,
        fidl_sme::Cbw::CbwCount => fidl_mlme::Cbw::CbwCount,
    }
}

fn connect(sme: &Arc<Mutex<Sme>>, ssid: Vec<u8>, password: Vec<u8>,
           txn: Option<ServerEnd<fidl_sme::ConnectTransactionMarker>>,
           params: fidl_sme::ConnectPhyParams)
    -> Result<(), failure::Error>
{
    let handle = match txn {
        None => None,
        Some(txn) => Some(txn.into_stream()?.control_handle())
    };

    let mlme_phy = convert_to_mlme_phy(params.phy);
    let mlme_cbw = convert_to_mlme_cbw(params.cbw);
    let params = ConnectPhyParams {
        phy: if params.override_phy { Some(mlme_phy) } else { None },
        cbw: if params.override_cbw { Some(mlme_cbw) } else { None },
    };
    sme.lock().unwrap().on_connect_command(ssid, password, handle, params);
    Ok(())
}

fn disconnect(sme: &Arc<Mutex<Sme>>) {
    sme.lock().unwrap().on_disconnect_command();
}

fn status(sme: &Arc<Mutex<Sme>>) -> fidl_sme::ClientStatusResponse {
    let status = sme.lock().unwrap().status();
    fidl_sme::ClientStatusResponse {
        connected_to: status.connected_to.map(|bss| {
            Box::new(convert_bss_info(bss))
        }),
        connecting_to_ssid: status.connecting_to.unwrap_or(Vec::new()),
    }
}

fn id_mismatch(this: u64, other: u64, id_type: &str) -> bool {
    if this != other {
        info!("Found an event with different {} than expected: {}, {}", id_type, this, other);
        return true;
    }
    return false;
}

fn handle_user_event(e: client_sme::UserEvent<Tokens>) {
    match e {
        client_sme::UserEvent::ScanFinished { tokens, result } =>
            send_all_scan_results(tokens, result),
        client_sme::UserEvent::ConnectFinished { token, result } => {
            send_connect_result(token, result).unwrap_or_else(|e| {
                if !is_peer_closed(&e) {
                    error!("Error sending connect result to user: {}", e);
                }
            })
        }
    }
}

fn handle_info_event(e: InfoEvent,
                     cobalt_sender: &mut CobaltSender,
                     connection_times: &mut ConnectionTimes,
) {
    match e {
        InfoEvent::ConnectStarted => {
            connection_times.connect_started_time = Some(zx::Time::get(zx::ClockId::Monotonic));
        },
        InfoEvent::ConnectFinished { result, failure } => {
            if let Some(connect_started_time) = connection_times.connect_started_time {
                let connection_finished_time = zx::Time::get(zx::ClockId::Monotonic);
                telemetry::report_connection_delay(cobalt_sender, connect_started_time,
                                              connection_finished_time, &result, &failure);
                connection_times.connect_started_time = None;
            }
        },
        InfoEvent::MlmeScanStart { txn_id } => {
            connection_times.txn_id = txn_id;
            connection_times.scan_started_time = Some(zx::Time::get(zx::ClockId::Monotonic));
        },
        InfoEvent::MlmeScanEnd { txn_id } => {
            if id_mismatch(txn_id, connection_times.txn_id, "ScanTxnId") {
                return;
            }
            if let Some(scan_started_time) = connection_times.scan_started_time {
                let scan_finished_time = zx::Time::get(zx::ClockId::Monotonic);
                telemetry::report_scan_delay(cobalt_sender, scan_started_time,
                                             scan_finished_time);
                connection_times.scan_started_time = None;
            }
        },
        InfoEvent::ScanDiscoveryFinished { bss_count, ess_count,
                                           num_bss_by_standard, num_bss_by_channel } => {
            telemetry::report_neighbor_networks_count(cobalt_sender, bss_count, ess_count);
            telemetry::report_standards(cobalt_sender, num_bss_by_standard);
            telemetry::report_channels(cobalt_sender, num_bss_by_channel);
        },
        InfoEvent::AssociationStarted { att_id } => {
            connection_times.att_id = att_id;
            connection_times.assoc_started_time = Some(zx::Time::get(zx::ClockId::Monotonic));
        },
        InfoEvent::AssociationSuccess { att_id } => {
            if id_mismatch(att_id, connection_times.att_id, "ConnectionAttemptId") {
                return;
            }
            if let Some(assoc_started_time) = connection_times.assoc_started_time {
                let assoc_finished_time = zx::Time::get(zx::ClockId::Monotonic);
                telemetry::report_assoc_success_delay(cobalt_sender, assoc_started_time,
                                             assoc_finished_time);
                connection_times.assoc_started_time = None;
            }
        },
        InfoEvent::RsnaStarted { att_id } => {
            connection_times.att_id = att_id;
            connection_times.rsna_started_time = Some(zx::Time::get(zx::ClockId::Monotonic));
        },
        InfoEvent::RsnaEstablished { att_id } => {
            if id_mismatch(att_id, connection_times.att_id, "ConnectionAttemptId") {
                return;
            }
            match connection_times.rsna_started_time {
                None => info!("Received UserEvent.RsnaEstablished before UserEvent.RsnaStarted"),
                Some(rsna_started_time) => {
                    let rsna_finished_time = zx::Time::get(zx::ClockId::Monotonic);
                    telemetry::report_rsna_established_delay(cobalt_sender, rsna_started_time,
                                                            rsna_finished_time);
                    connection_times.rsna_started_time = None;
                }
            }
        },
    }
}

fn send_all_scan_results(tokens: Vec<fidl_sme::ScanTransactionControlHandle>,
                         result: EssDiscoveryResult) {
    let mut fidl_result = result
        .map(|ess_list| ess_list.into_iter().map(convert_ess_info).collect::<Vec<_>>())
        .map_err(|e| {
            fidl_sme::ScanError {
                code: match e {
                    DiscoveryError::NotSupported => fidl_sme::ScanErrorCode::NotSupported,
                    DiscoveryError::InternalError => fidl_sme::ScanErrorCode::InternalError,
                },
                message: e.to_string()
            }
        });
    for token in tokens {
        send_scan_results(token, &mut fidl_result).unwrap_or_else(|e| {
            if !is_peer_closed(&e) {
                error!("Error sending scan results to user: {}", e);
            }
        })
    }
}

fn send_scan_results(token: fidl_sme::ScanTransactionControlHandle,
                     result: &mut Result<Vec<fidl_sme::EssInfo>, fidl_sme::ScanError>)
    -> Result<(), fidl::Error>
{
    match result {
        Ok(ess_list) => {
            token.send_on_result(&mut ess_list.iter_mut())?;
            token.send_on_finished()?;
        },
        Err(e) => token.send_on_error(e)?
    }
    Ok(())
}

fn convert_ess_info(ess: EssInfo) -> fidl_sme::EssInfo {
    fidl_sme::EssInfo {
        best_bss: convert_bss_info(ess.best_bss),
    }
}

fn convert_bss_info(bss: BssInfo) -> fidl_sme::BssInfo {
    fidl_sme::BssInfo {
        bssid: bss.bssid,
        ssid: bss.ssid,
        rx_dbm: bss.rx_dbm,
        channel: bss.channel,
        protected: bss.protected,
        compatible: bss.compatible
    }
}

fn send_connect_result(token: Option<fidl_sme::ConnectTransactionControlHandle>,
                       result: ConnectResult)
    -> Result<(), fidl::Error>
{
    if let Some(token) = token {
        let code = match result {
            ConnectResult::Success => fidl_sme::ConnectResultCode::Success,
            ConnectResult::Canceled => fidl_sme::ConnectResultCode::Canceled,
            ConnectResult::Failed => fidl_sme::ConnectResultCode::Failed,
            ConnectResult::BadCredentials => fidl_sme::ConnectResultCode::BadCredentials,
        };
        token.send_on_finished(code)?;
    }
    Ok(())
}

