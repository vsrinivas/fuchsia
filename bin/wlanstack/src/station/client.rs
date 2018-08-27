// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async::temp::TempFutureExt;
use failure::{format_err, ResultExt};
use fidl::{endpoints2::RequestStream, endpoints2::ServerEnd};
use fidl_fuchsia_wlan_mlme::{MlmeEventStream, MlmeProxy};
use fidl_fuchsia_wlan_sme::{self as fidl_sme, ClientSmeRequest};
use futures::{prelude::*, stream};
use futures::channel::mpsc;
use log::{error, log};
use std::sync::{Arc, Mutex};
use wlan_sme::{client as client_sme, DeviceInfo};
use wlan_sme::client::{BssInfo, ConnectResult, DiscoveryError, DiscoveryResult, EssInfo};

use crate::fidl_util::is_peer_closed;
use crate::Never;
use crate::stats_scheduler::StatsRequest;

struct Tokens;

impl client_sme::Tokens for Tokens {
    type ScanToken = fidl_sme::ScanTransactionControlHandle;
    type ConnectToken = Option<fidl_sme::ConnectTransactionControlHandle>;
}

pub type Endpoint = ServerEnd<fidl_sme::ClientSmeMarker>;
type Sme = client_sme::ClientSme<Tokens>;

pub fn serve<S>(proxy: MlmeProxy,
                device_info: DeviceInfo,
                event_stream: MlmeEventStream,
                new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
                stats_requests: S)
    -> impl Future<Output = Result<(), failure::Error>>
    where S: Stream<Item = StatsRequest>
{
    let (sme, mlme_stream, user_stream) = Sme::new(device_info);
    let sme = Arc::new(Mutex::new(sme));
    let mlme_sme_fut = super::serve_mlme_sme(
        proxy, event_stream, Arc::clone(&sme), mlme_stream, stats_requests);
    let sme_fidl_fut = serve_fidl(sme, new_fidl_clients, user_stream);
    mlme_sme_fut.select(sme_fidl_fut)
        .map(|e| e.either(|x| Ok(x.context("MLME<->SME future")?),
                          |x| Ok(x.context("SME<->FIDL future")?.into_any())))
}

fn serve_fidl(sme: Arc<Mutex<Sme>>,
              new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
              user_stream: client_sme::UserStream<Tokens>)
    -> impl Future<Output = Result<Never, failure::Error>>
{
    const SERVE_LIMIT: usize = 1000;

    // A future that forwards user events from the station to connected FIDL clients
    let sme_to_fidl = serve_user_stream(user_stream)
        .map(|()| Err(format_err!("SME->FIDL future unexpectedly finished")));
    // A future that handles requests from FIDL clients
    let fidl_to_sme = new_fidl_clients
        .map(Ok)
        .chain(stream::once(
            future::ready(Err(format_err!("new FIDL client stream unexpectedly ended")))))
        .try_for_each_concurrent(SERVE_LIMIT, move |channel| {
            serve_fidl_endpoint(Arc::clone(&sme), channel).unwrap_or_else(
                |e| error!("Error handling a FIDL request from user: {:?}", e)
            ).map(Ok)
        })
        .map(|x| match x {
            Ok(()) => Err(format_err!("FIDL->SME future unexpectedly finished")),
            Err(e) => Err(e),
        });
    sme_to_fidl.try_join(fidl_to_sme).map_ok(|x: (Never, Never)| x.0)
}

fn serve_fidl_endpoint(sme: Arc<Mutex<Sme>>, endpoint: Endpoint)
    -> impl Future<Output = Result<(), ::fidl::Error>>
{
    future::ready(endpoint.into_stream())
        .and_then(|s| {
            s.try_for_each(move |request| future::ready(match request {
                ClientSmeRequest::Scan { txn, .. } => {
                    Ok(scan(&sme, txn)
                        .unwrap_or_else(|e| error!("Error starting a scan transaction: {:?}", e)))
                },
                ClientSmeRequest::Connect { req, txn, .. } => {
                    Ok(connect(&sme, req.ssid, req.password, txn)
                        .unwrap_or_else(|e| error!("Error starting a connect transaction: {:?}", e)))
                },
                ClientSmeRequest::Disconnect { responder } => {
                    disconnect(&sme);
                    responder.send()
                }
                ClientSmeRequest::Status { responder } => responder.send(&mut status(&sme)),
            }))
        })
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
    sme.lock().unwrap().on_connect_command(ssid, password, handle);
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

fn serve_user_stream(stream: client_sme::UserStream<Tokens>) -> impl Future<Output = ()> {
    stream
        .for_each(|e| {
            future::ready(match e {
                client_sme::UserEvent::ScanFinished{ tokens, result } =>
                    send_all_scan_results(tokens, result),
                client_sme::UserEvent::ConnectFinished { token, result } => {
                    send_connect_result(token, result).unwrap_or_else(|e| {
                        if !is_peer_closed(&e) {
                            error!("Error sending connect result to user: {}", e);
                        }
                    })
                }
            })
        })
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
