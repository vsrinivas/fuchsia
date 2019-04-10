// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::bail;
use fidl::{endpoints::RequestStream, endpoints::ServerEnd};
use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_mlme::{MlmeEventStream, MlmeProxy};
use fidl_fuchsia_wlan_sme::{self as fidl_sme, ClientSmeRequest};
use fuchsia_zircon as zx;
use futures::channel::mpsc;
use futures::{prelude::*, select, stream::FuturesUnordered};
use log::{error, info};
use pin_utils::pin_mut;
use std::marker::Unpin;
use std::sync::{Arc, Mutex};
use void::Void;
use wlan_inspect;
use wlan_sme::client::{
    BssInfo, ConnectResult, ConnectionAttemptId, DiscoveryError, EssDiscoveryResult, EssInfo,
    InfoEvent, ScanTxnId,
};
use wlan_sme::{client as client_sme, DeviceInfo, InfoStream};

use crate::fidl_util::is_peer_closed;
use crate::stats_scheduler::StatsRequest;
use crate::telemetry;
use fuchsia_cobalt::CobaltSender;

pub type Endpoint = ServerEnd<fidl_sme::ClientSmeMarker>;
type Sme = client_sme::ClientSme;

struct ConnectionTimes {
    att_id: ConnectionAttemptId,
    txn_id: ScanTxnId,
    connect_started_time: Option<zx::Time>,
    scan_started_time: Option<zx::Time>,
    assoc_started_time: Option<zx::Time>,
    rsna_started_time: Option<zx::Time>,
}

pub async fn serve<S>(
    proxy: MlmeProxy,
    device_info: DeviceInfo,
    event_stream: MlmeEventStream,
    new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
    stats_requests: S,
    cobalt_sender: CobaltSender,
    inspect_sme: wlan_inspect::nodes::SharedNodePtr,
) -> Result<(), failure::Error>
where
    S: Stream<Item = StatsRequest> + Unpin,
{
    let (sme, mlme_stream, info_stream, time_stream) = Sme::new(device_info, inspect_sme);
    let sme = Arc::new(Mutex::new(sme));
    let mlme_sme = super::serve_mlme_sme(
        proxy,
        event_stream,
        Arc::clone(&sme),
        mlme_stream,
        stats_requests,
        time_stream,
    );
    let sme_fidl = serve_fidl(sme, new_fidl_clients, info_stream, cobalt_sender);
    pin_mut!(mlme_sme);
    pin_mut!(sme_fidl);
    Ok(select! {
        mlme_sme = mlme_sme.fuse() => mlme_sme?,
        sme_fidl = sme_fidl.fuse() => match sme_fidl? {},
    })
}

async fn serve_fidl(
    sme: Arc<Mutex<Sme>>,
    new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
    info_stream: InfoStream,
    mut cobalt_sender: CobaltSender,
) -> Result<Void, failure::Error> {
    let mut new_fidl_clients = new_fidl_clients.fuse();
    let mut info_stream = info_stream.fuse();
    let mut fidl_clients = FuturesUnordered::new();
    let mut connection_times = ConnectionTimes {
        att_id: 0,
        txn_id: 0,
        connect_started_time: None,
        scan_started_time: None,
        assoc_started_time: None,
        rsna_started_time: None,
    };
    loop {
        select! {
            info_event = info_stream.next() => match info_event {
                Some(e) => handle_info_event(e, &mut cobalt_sender, &mut connection_times),
                None => bail!("Info Event stream unexpectedly ended"),
            },
            new_fidl_client = new_fidl_clients.next() => match new_fidl_client {
                Some(c) => fidl_clients.push(serve_fidl_endpoint(&sme, c)),
                None => bail!("New FIDL client stream unexpectedly ended"),
            },
            () = fidl_clients.select_next_some() => {},
        }
    }
}

async fn serve_fidl_endpoint(sme: &Mutex<Sme>, endpoint: Endpoint) {
    let stream = match endpoint.into_stream() {
        Ok(s) => s,
        Err(e) => {
            error!("Failed to create a stream from a zircon channel: {}", e);
            return;
        }
    };
    const MAX_CONCURRENT_REQUESTS: usize = 1000;
    let r = await!(stream.try_for_each_concurrent(MAX_CONCURRENT_REQUESTS, move |request| {
        handle_fidl_request(sme, request)
    }));
    if let Err(e) = r {
        error!("Error serving FIDL: {}", e);
        return;
    }
}

async fn handle_fidl_request(
    sme: &Mutex<Sme>,
    request: fidl_sme::ClientSmeRequest,
) -> Result<(), fidl::Error> {
    match request {
        ClientSmeRequest::Scan { req, txn, .. } => Ok(await!(scan(sme, txn, req.scan_type))
            .unwrap_or_else(|e| error!("Error handling a scan transaction: {:?}", e))),
        ClientSmeRequest::Connect { req, txn, .. } => Ok(await!(connect(sme, txn, req))
            .unwrap_or_else(|e| error!("Error handling a connect transaction: {:?}", e))),
        ClientSmeRequest::Disconnect { responder } => {
            disconnect(sme);
            responder.send()
        }
        ClientSmeRequest::Status { responder } => responder.send(&mut status(sme)),
    }
}

async fn scan(
    sme: &Mutex<Sme>,
    txn: ServerEnd<fidl_sme::ScanTransactionMarker>,
    scan_type: fidl_common::ScanType,
) -> Result<(), failure::Error> {
    let handle = txn.into_stream()?.control_handle();
    let receiver = sme.lock().unwrap().on_scan_command(scan_type);
    let result = await!(receiver).unwrap_or(Err(DiscoveryError::InternalError));
    let send_result = send_scan_results(handle, result);
    filter_out_peer_closed(send_result)?;
    Ok(())
}

async fn connect(
    sme: &Mutex<Sme>,
    txn: Option<ServerEnd<fidl_sme::ConnectTransactionMarker>>,
    req: fidl_sme::ConnectRequest,
) -> Result<(), failure::Error> {
    let handle = match txn {
        None => None,
        Some(txn) => Some(txn.into_stream()?.control_handle()),
    };
    let receiver = sme.lock().unwrap().on_connect_command(req);
    let result = await!(receiver).unwrap_or(ConnectResult::Failed);
    let send_result = send_connect_result(handle, result);
    filter_out_peer_closed(send_result)?;
    Ok(())
}

pub fn filter_out_peer_closed(r: Result<(), fidl::Error>) -> Result<(), fidl::Error> {
    match r {
        Err(ref e) if is_peer_closed(e) => Ok(()),
        other => other,
    }
}

fn disconnect(sme: &Mutex<Sme>) {
    sme.lock().unwrap().on_disconnect_command();
}

fn status(sme: &Mutex<Sme>) -> fidl_sme::ClientStatusResponse {
    let status = sme.lock().unwrap().status();
    fidl_sme::ClientStatusResponse {
        connected_to: status.connected_to.map(|bss| Box::new(convert_bss_info(bss))),
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

fn handle_info_event(
    e: InfoEvent,
    cobalt_sender: &mut CobaltSender,
    connection_times: &mut ConnectionTimes,
) {
    match e {
        InfoEvent::ConnectStarted => {
            connection_times.connect_started_time = Some(zx::Time::get(zx::ClockId::Monotonic));
        }
        InfoEvent::ConnectFinished { result, failure } => {
            if let Some(connect_started_time) = connection_times.connect_started_time {
                let connection_finished_time = zx::Time::get(zx::ClockId::Monotonic);
                telemetry::report_connection_delay(
                    cobalt_sender,
                    connect_started_time,
                    connection_finished_time,
                    &result,
                    &failure,
                );
                connection_times.connect_started_time = None;
            }
        }
        InfoEvent::MlmeScanStart { txn_id } => {
            connection_times.txn_id = txn_id;
            connection_times.scan_started_time = Some(zx::Time::get(zx::ClockId::Monotonic));
        }
        InfoEvent::MlmeScanEnd { txn_id } => {
            if id_mismatch(txn_id, connection_times.txn_id, "ScanTxnId") {
                return;
            }
            if let Some(scan_started_time) = connection_times.scan_started_time {
                let scan_finished_time = zx::Time::get(zx::ClockId::Monotonic);
                telemetry::report_scan_delay(cobalt_sender, scan_started_time, scan_finished_time);
                connection_times.scan_started_time = None;
            }
        }
        InfoEvent::ScanDiscoveryFinished {
            bss_count,
            ess_count,
            num_bss_by_standard,
            num_bss_by_channel,
        } => {
            telemetry::report_neighbor_networks_count(cobalt_sender, bss_count, ess_count);
            telemetry::report_standards(cobalt_sender, num_bss_by_standard);
            telemetry::report_channels(cobalt_sender, num_bss_by_channel);
        }
        InfoEvent::AssociationStarted { att_id } => {
            connection_times.att_id = att_id;
            connection_times.assoc_started_time = Some(zx::Time::get(zx::ClockId::Monotonic));
        }
        InfoEvent::AssociationSuccess { att_id } => {
            if id_mismatch(att_id, connection_times.att_id, "ConnectionAttemptId") {
                return;
            }
            if let Some(assoc_started_time) = connection_times.assoc_started_time {
                let assoc_finished_time = zx::Time::get(zx::ClockId::Monotonic);
                telemetry::report_assoc_success_delay(
                    cobalt_sender,
                    assoc_started_time,
                    assoc_finished_time,
                );
                connection_times.assoc_started_time = None;
            }
        }
        InfoEvent::RsnaStarted { att_id } => {
            connection_times.att_id = att_id;
            connection_times.rsna_started_time = Some(zx::Time::get(zx::ClockId::Monotonic));
        }
        InfoEvent::RsnaEstablished { att_id } => {
            if id_mismatch(att_id, connection_times.att_id, "ConnectionAttemptId") {
                return;
            }
            match connection_times.rsna_started_time {
                None => info!("Received UserEvent.RsnaEstablished before UserEvent.RsnaStarted"),
                Some(rsna_started_time) => {
                    let rsna_finished_time = zx::Time::get(zx::ClockId::Monotonic);
                    telemetry::report_rsna_established_delay(
                        cobalt_sender,
                        rsna_started_time,
                        rsna_finished_time,
                    );
                    connection_times.rsna_started_time = None;
                }
            }
        }
    }
}

fn send_scan_results(
    handle: fidl_sme::ScanTransactionControlHandle,
    result: EssDiscoveryResult,
) -> Result<(), fidl::Error> {
    match result {
        Ok(ess_list) => {
            let mut fidl_list = ess_list.into_iter().map(convert_ess_info).collect::<Vec<_>>();
            handle.send_on_result(&mut fidl_list.iter_mut())?;
            handle.send_on_finished()?;
        }
        Err(e) => {
            let mut fidl_err = fidl_sme::ScanError {
                code: match e {
                    DiscoveryError::NotSupported => fidl_sme::ScanErrorCode::NotSupported,
                    DiscoveryError::InternalError => fidl_sme::ScanErrorCode::InternalError,
                },
                message: e.to_string(),
            };
            handle.send_on_error(&mut fidl_err)?;
        }
    }
    Ok(())
}

fn convert_ess_info(ess: EssInfo) -> fidl_sme::EssInfo {
    fidl_sme::EssInfo { best_bss: convert_bss_info(ess.best_bss) }
}

fn convert_bss_info(bss: BssInfo) -> fidl_sme::BssInfo {
    fidl_sme::BssInfo {
        bssid: bss.bssid,
        ssid: bss.ssid,
        rx_dbm: bss.rx_dbm,
        channel: bss.channel,
        protected: bss.protected,
        compatible: bss.compatible,
    }
}

fn send_connect_result(
    handle: Option<fidl_sme::ConnectTransactionControlHandle>,
    result: ConnectResult,
) -> Result<(), fidl::Error> {
    if let Some(handle) = handle {
        let code = match result {
            ConnectResult::Success => fidl_sme::ConnectResultCode::Success,
            ConnectResult::Canceled => fidl_sme::ConnectResultCode::Canceled,
            ConnectResult::Failed => fidl_sme::ConnectResultCode::Failed,
            ConnectResult::BadCredentials => fidl_sme::ConnectResultCode::BadCredentials,
        };
        handle.send_on_finished(code)?;
    }
    Ok(())
}
