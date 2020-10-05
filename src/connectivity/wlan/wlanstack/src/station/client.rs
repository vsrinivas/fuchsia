// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use fidl::{endpoints::RequestStream, endpoints::ServerEnd};
use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEventStream, MlmeProxy};
use fidl_fuchsia_wlan_sme::{self as fidl_sme, ClientSmeRequest};
use futures::channel::mpsc;
use futures::{prelude::*, select, stream::FuturesUnordered};
use log::error;
use pin_utils::pin_mut;
use std::marker::Unpin;
use std::sync::{Arc, Mutex};
use void::Void;
use wlan_inspect;
use wlan_sme::client::{
    BssDiscoveryResult, BssInfo, ConnectFailure, ConnectResult, EstablishRsnaFailure, InfoEvent,
    SelectNetworkFailure,
};
use wlan_sme::{self as sme, client as client_sme, InfoStream};

use crate::stats_scheduler::StatsRequest;
use crate::telemetry;
use fuchsia_cobalt::CobaltSender;

pub type Endpoint = ServerEnd<fidl_sme::ClientSmeMarker>;
type Sme = client_sme::ClientSme;

pub async fn serve<S>(
    cfg: sme::Config,
    proxy: MlmeProxy,
    device_info: fidl_mlme::DeviceInfo,
    event_stream: MlmeEventStream,
    new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
    stats_requests: S,
    cobalt_sender: CobaltSender,
    iface_tree_holder: Arc<wlan_inspect::iface_mgr::IfaceTreeHolder>,
    inspect_hash_key: [u8; 8],
) -> Result<(), anyhow::Error>
where
    S: Stream<Item = StatsRequest> + Unpin,
{
    let wpa3_supported =
        device_info.driver_features.iter().any(|f| f == &fidl_common::DriverFeature::SaeSmeAuth);
    let cfg = client_sme::ClientConfig::from_config(cfg, wpa3_supported);
    let is_softmac = device_info.driver_features.contains(&fidl_common::DriverFeature::TempSoftmac);
    let (sme, mlme_stream, info_stream, time_stream) =
        Sme::new(cfg, device_info, iface_tree_holder, inspect_hash_key, is_softmac);
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
) -> Result<Void, anyhow::Error> {
    let mut new_fidl_clients = new_fidl_clients.fuse();
    let mut info_stream = info_stream.fuse();
    let mut fidl_clients = FuturesUnordered::new();
    loop {
        select! {
            info_event = info_stream.next() => match info_event {
                Some(e) => handle_info_event(e, &mut cobalt_sender),
                None => return Err(format_err!("Info Event stream unexpectedly ended")),
            },
            new_fidl_client = new_fidl_clients.next() => match new_fidl_client {
                Some(c) => fidl_clients.push(serve_fidl_endpoint(&sme, c)),
                None => return Err(format_err!("New FIDL client stream unexpectedly ended")),
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
    let r = stream
        .try_for_each_concurrent(MAX_CONCURRENT_REQUESTS, move |request| {
            handle_fidl_request(sme, request)
        })
        .await;
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
        ClientSmeRequest::Scan { req, txn, .. } => Ok(scan(sme, txn, req)
            .await
            .unwrap_or_else(|e| error!("Error handling a scan transaction: {:?}", e))),
        ClientSmeRequest::Connect { req, txn, .. } => Ok(connect(sme, txn, req)
            .await
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
    scan_request: fidl_sme::ScanRequest,
) -> Result<(), anyhow::Error> {
    let handle = txn.into_stream()?.control_handle();
    let receiver = sme.lock().unwrap().on_scan_command(scan_request);
    let result = receiver.await.unwrap_or(Err(fidl_mlme::ScanResultCodes::InternalError));
    let send_result = send_scan_results(handle, result);
    filter_out_peer_closed(send_result)?;
    Ok(())
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
    let receiver = sme.lock().unwrap().on_connect_command(req);
    let result = receiver.await.ok();
    let send_result = send_connect_result(handle, result);
    filter_out_peer_closed(send_result)?;
    Ok(())
}

pub fn filter_out_peer_closed(r: Result<(), fidl::Error>) -> Result<(), fidl::Error> {
    match r {
        Err(ref e) if e.is_closed() => Ok(()),
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

fn handle_info_event(e: InfoEvent, cobalt_sender: &mut CobaltSender) {
    match e {
        InfoEvent::DiscoveryScanStats(scan_stats) => {
            let is_join_scan = false;
            telemetry::log_scan_stats(cobalt_sender, &scan_stats, is_join_scan);
        }
        InfoEvent::ConnectStats(connect_stats) => {
            telemetry::log_connect_stats(cobalt_sender, &connect_stats)
        }
        InfoEvent::ConnectionPing(info) => telemetry::log_connection_ping(cobalt_sender, &info),
        InfoEvent::DisconnectInfo(info) => telemetry::log_disconnect(cobalt_sender, &info),
    }
}

fn send_scan_results(
    handle: fidl_sme::ScanTransactionControlHandle,
    result: BssDiscoveryResult,
) -> Result<(), fidl::Error> {
    match result {
        Ok(bss_list) => {
            let mut fidl_list = bss_list.into_iter().map(convert_bss_info).collect::<Vec<_>>();
            handle.send_on_result(&mut fidl_list.iter_mut())?;
            handle.send_on_finished()?;
        }
        Err(e) => {
            let mut fidl_err = match e {
                fidl_mlme::ScanResultCodes::NotSupported => fidl_sme::ScanError {
                    code: fidl_sme::ScanErrorCode::NotSupported,
                    message: "Scanning not supported by device".to_string(),
                },
                _ => fidl_sme::ScanError {
                    code: fidl_sme::ScanErrorCode::InternalError,
                    message: "Internal error occurred".to_string(),
                },
            };
            handle.send_on_error(&mut fidl_err)?;
        }
    }
    Ok(())
}

fn convert_bss_info(bss: BssInfo) -> fidl_sme::BssInfo {
    fidl_sme::BssInfo {
        bssid: bss.bssid,
        ssid: bss.ssid,
        rx_dbm: bss.rx_dbm,
        snr_db: bss.snr_db,
        channel: bss.channel,
        protection: bss.protection.into(),
        compatible: bss.compatible,
    }
}

fn convert_connect_result(result: &ConnectResult) -> fidl_sme::ConnectResultCode {
    match result {
        ConnectResult::Success => fidl_sme::ConnectResultCode::Success,
        ConnectResult::Canceled => fidl_sme::ConnectResultCode::Canceled,
        // This case is for errors associated with the credential type specified.
        // Example problems are specifying an unsupported protection type or
        // specifying a password for an open network.
        ConnectResult::Failed(ConnectFailure::SelectNetwork(
            SelectNetworkFailure::CredentialError(_),
        )) => fidl_sme::ConnectResultCode::WrongCredentialType,
        // Assuming the correct type of credentials are given, a bad password
        // will cause EstablishRsnaFailure::KeyFrameExchangeTimeout. This error
        // is not returned if and only if a bad password is given, but a bad
        // password is the most likely cause. The authenticator will silently
        // drop EAPOL handshake frames when the password is wrong.
        //
        // NOTE: The alternative possibilities for seeing an
        // EstablishRsnaFailure::KeyFrameExchangeTimeout are an error in
        // our crypto parameter parsing and crypto implementation, or a lost
        // connection with the AP.
        ConnectResult::Failed(ConnectFailure::EstablishRsna(
            EstablishRsnaFailure::KeyFrameExchangeTimeout,
        )) => fidl_sme::ConnectResultCode::CredentialRejected,
        ConnectResult::Failed(..) => fidl_sme::ConnectResultCode::Failed,
    }
}

fn send_connect_result(
    handle: Option<fidl_sme::ConnectTransactionControlHandle>,
    result: Option<ConnectResult>,
) -> Result<(), fidl::Error> {
    if let Some(handle) = handle {
        let code = match result {
            Some(connect_result) => {
                if let ConnectResult::Failed(_) = connect_result {
                    error!("Connection failed: {:?}", connect_result);
                }
                convert_connect_result(&connect_result)
            }
            None => {
                error!("Connection failed. No result from SME.");
                fidl_sme::ConnectResultCode::Failed
            }
        };
        handle.send_on_finished(code)?;
    }
    Ok(())
}
