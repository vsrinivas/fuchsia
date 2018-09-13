// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::bail;
use fidl::{endpoints2::RequestStream, endpoints2::ServerEnd};
use fidl_fuchsia_wlan_mlme::{MlmeEventStream, MlmeProxy};
use fidl_fuchsia_wlan_sme::{self as fidl_sme, ClientSmeRequest};
use futures::{prelude::*, select};
use futures::channel::mpsc;
use log::error;
use pin_utils::pin_mut;
use std::marker::Unpin;
use std::sync::{Arc, Mutex};
use wlan_sme::{client as client_sme, DeviceInfo};
use wlan_sme::client::{BssInfo, ConnectResult, DiscoveryError, DiscoveryResult, EssInfo};
use fuchsia_zircon as zx;

use crate::cobalt_reporter::CobaltSender;
use crate::fidl_util::is_peer_closed;
use crate::future_util::ConcurrentTasks;
use crate::Never;
use crate::stats_scheduler::StatsRequest;
use crate::telemetry;

struct Tokens;

struct ConnectToken {
    handle: Option<fidl_sme::ConnectTransactionControlHandle>,
    time_started: zx::Time,
}

impl client_sme::Tokens for Tokens {
    type ScanToken = fidl_sme::ScanTransactionControlHandle;
    type ConnectToken = ConnectToken;
}

pub type Endpoint = ServerEnd<fidl_sme::ClientSmeMarker>;
type Sme = client_sme::ClientSme<Tokens>;

pub async fn serve<S>(proxy: MlmeProxy,
                      device_info: DeviceInfo,
                      event_stream: MlmeEventStream,
                      new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
                      stats_requests: S,
                      cobalt_sender: CobaltSender)
    -> Result<(), failure::Error>
    where S: Stream<Item = StatsRequest> + Unpin
{
    let (sme, mlme_stream, user_stream) = Sme::new(device_info);
    let sme = Arc::new(Mutex::new(sme));
    let mlme_sme = super::serve_mlme_sme(
        proxy, event_stream, Arc::clone(&sme), mlme_stream, stats_requests);
    let sme_fidl = serve_fidl(sme, new_fidl_clients, user_stream, cobalt_sender);
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
                    mut cobalt_sender: CobaltSender)
    -> Result<Never, failure::Error>
{
    let mut fidl_clients = ConcurrentTasks::new();
    loop {
        let mut user_event = user_stream.next();
        let mut new_fidl_client = new_fidl_clients.next();
        select! {
            user_event => match user_event {
                Some(e) => handle_user_event(e, &mut cobalt_sender),
                None => bail!("SME->FIDL future unexpectedly finished"),
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
            Ok(connect(sme, req.ssid, req.password, txn)
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

fn connect(sme: &Arc<Mutex<Sme>>, ssid: Vec<u8>, password: Vec<u8>,
           txn: Option<ServerEnd<fidl_sme::ConnectTransactionMarker>>)
    -> Result<(), failure::Error>
{
    let handle = match txn {
        None => None,
        Some(txn) => Some(txn.into_stream()?.control_handle())
    };
    let token = ConnectToken {
        handle,
        time_started: zx::Time::get(zx::ClockId::Monotonic),
    };
    sme.lock().unwrap().on_connect_command(ssid, password, token);
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

fn handle_user_event(e: client_sme::UserEvent<Tokens>,
                     cobalt_sender: &mut CobaltSender,
) {
    match e {
        client_sme::UserEvent::ScanFinished{ tokens, result } =>
            send_all_scan_results(tokens, result),
        client_sme::UserEvent::ConnectFinished { token, result } => {
            let (time, result_index) = get_connection_time_observation(token.time_started, &result);
            telemetry::report_connection_time(cobalt_sender, time, result_index);
            send_connect_result(token.handle, result).unwrap_or_else(|e| {
                if !is_peer_closed(&e) {
                    error!("Error sending connect result to user: {}", e);
                }
            })
        }
    }
}

fn send_all_scan_results(tokens: Vec<fidl_sme::ScanTransactionControlHandle>,
                         result: DiscoveryResult) {
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
        best_bss: convert_bss_info(ess.best_bss)
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

fn get_connection_time_observation(time_started: zx::Time, result: &client_sme::ConnectResult
) -> (i64, u32) {
    let time_now = zx::Time::get(zx::ClockId::Monotonic);
    let connection_time_micros = (time_now - time_started).nanos() / 1000;
    let result_index = match result {
        client_sme::ConnectResult::Success => 0,
        _ => 1,
    };
    (connection_time_micros, result_index)
}
