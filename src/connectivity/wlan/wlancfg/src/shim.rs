// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{client, known_ess_store::KnownEssStore};

use fidl::{self, endpoints::create_proxy};
use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_device_service as wlan_service;
use fidl_fuchsia_wlan_service as legacy;
use fidl_fuchsia_wlan_sme as fidl_sme;
use fidl_fuchsia_wlan_stats as fidl_wlan_stats;
use fuchsia_zircon as zx;
use futures::{channel::oneshot, prelude::*};
use itertools::Itertools;
use std::sync::{Arc, Mutex};

#[derive(Clone)]
pub struct Client {
    pub service: wlan_service::DeviceServiceProxy,
    pub client: client::Client,
    pub sme: fidl_sme::ClientSmeProxy,
    pub iface_id: u16,
}

#[derive(Clone)]
pub struct ClientRef(Arc<Mutex<Option<Client>>>);

impl ClientRef {
    pub fn new() -> Self {
        ClientRef(Arc::new(Mutex::new(None)))
    }

    pub fn set_if_empty(&self, client: Client) {
        let mut c = self.0.lock().unwrap();
        if c.is_none() {
            *c = Some(client);
        }
    }

    pub fn remove_if_matching(&self, iface_id: u16) {
        let mut c = self.0.lock().unwrap();
        let same_id = match *c {
            Some(ref c) => c.iface_id == iface_id,
            None => false,
        };
        if same_id {
            *c = None;
        }
    }

    pub fn get(&self) -> Result<Client, legacy::Error> {
        self.0.lock().unwrap().clone().ok_or_else(|| legacy::Error {
            code: legacy::ErrCode::NotFound,
            description: "No wireless interface found".to_string(),
        })
    }
}

const MAX_CONCURRENT_WLAN_REQUESTS: usize = 1000;

pub async fn serve_legacy(
    requests: legacy::WlanRequestStream,
    client: ClientRef,
    ess_store: Arc<KnownEssStore>,
) -> Result<(), fidl::Error> {
    await!(requests.try_for_each_concurrent(MAX_CONCURRENT_WLAN_REQUESTS, |req| handle_request(
        &client,
        req,
        Arc::clone(&ess_store)
    )))
}

async fn handle_request(
    client: &ClientRef,
    req: legacy::WlanRequest,
    ess_store: Arc<KnownEssStore>,
) -> Result<(), fidl::Error> {
    match req {
        legacy::WlanRequest::Scan { req, responder } => {
            let mut r = await!(scan(client, req));
            responder.send(&mut r)
        }
        legacy::WlanRequest::Connect { req, responder } => {
            let mut r = await!(connect(&client, req));
            responder.send(&mut r)
        }
        legacy::WlanRequest::Disconnect { responder } => {
            let mut r = await!(disconnect(client.clone()));
            responder.send(&mut r)
        }
        legacy::WlanRequest::Status { responder } => {
            let mut r = await!(status(&client));
            responder.send(&mut r)
        }
        legacy::WlanRequest::StartBss { responder, .. } => {
            eprintln!("StartBss() is not implemented");
            responder.send(&mut not_supported())
        }
        legacy::WlanRequest::StopBss { responder } => {
            eprintln!("StopBss() is not implemented");
            responder.send(&mut not_supported())
        }
        legacy::WlanRequest::Stats { responder } => {
            let mut r = await!(stats(client));
            responder.send(&mut r)
        }
        legacy::WlanRequest::ClearSavedNetworks { responder } => {
            if let Err(e) = ess_store.clear() {
                eprintln!("Error clearing known ESS: {}", e);
            }
            responder.send()
        }
    }
}

async fn scan(client: &ClientRef, legacy_req: legacy::ScanRequest) -> legacy::ScanResult {
    let r = await!(async move {
        let client = client.get()?;
        let scan_txn = start_scan_txn(&client, legacy_req).map_err(|e| {
            eprintln!("Failed to start a scan transaction: {}", e);
            internal_error()
        })?;

        let mut evt_stream = scan_txn.take_event_stream();
        let mut aps = vec![];
        let mut done = false;

        while let Some(event) = await!(evt_stream.try_next()).map_err(|e| {
            eprintln!("Error reading from scan transaction stream: {}", e);
            internal_error()
        })? {
            match event {
                fidl_sme::ScanTransactionEvent::OnResult { aps: new_aps } => {
                    aps.extend(new_aps);
                    done = false;
                }
                fidl_sme::ScanTransactionEvent::OnFinished {} => done = true,
                fidl_sme::ScanTransactionEvent::OnError { error } => {
                    return Err(convert_scan_err(error));
                }
            }
        }

        if !done {
            eprintln!("Failed to fetch all results before the channel was closed");
            return Err(internal_error());
        }

        Ok(aps
            .iter()
            .map(|ess| convert_bss_info(&ess.best_bss))
            .sorted_by(|a, b| a.ssid.cmp(&b.ssid))
            .collect())
    });

    match r {
        Ok(aps) => legacy::ScanResult { error: success(), aps: Some(aps) },
        Err(error) => legacy::ScanResult { error, aps: None },
    }
}

fn start_scan_txn(
    client: &Client,
    legacy_req: legacy::ScanRequest,
) -> Result<fidl_sme::ScanTransactionProxy, fidl::Error> {
    let (scan_txn, remote) = create_proxy()?;
    let mut req = fidl_sme::ScanRequest {
        timeout: legacy_req.timeout,
        scan_type: fidl_common::ScanType::Passive,
    };
    client.sme.scan(&mut req, remote)?;
    Ok(scan_txn)
}

fn convert_scan_err(error: fidl_sme::ScanError) -> legacy::Error {
    legacy::Error {
        code: match error.code {
            fidl_sme::ScanErrorCode::NotSupported => legacy::ErrCode::NotSupported,
            fidl_sme::ScanErrorCode::InternalError => legacy::ErrCode::Internal,
        },
        description: error.message,
    }
}

fn convert_bss_info(bss: &fidl_sme::BssInfo) -> legacy::Ap {
    legacy::Ap {
        bssid: bss.bssid.to_vec(),
        ssid: String::from_utf8_lossy(&bss.ssid).to_string(),
        rssi_dbm: bss.rx_dbm,
        is_secure: bss.protected,
        is_compatible: bss.compatible,
        chan: fidl_common::WlanChan {
            primary: bss.channel,
            secondary80: 0,
            cbw: fidl_common::Cbw::Cbw20,
        },
    }
}

fn connect(
    client: &ClientRef,
    legacy_req: legacy::ConnectConfig,
) -> impl Future<Output = legacy::Error> {
    future::ready(client.get())
        .and_then(move |client| {
            let (responder, receiver) = oneshot::channel();
            let req = client::ConnectRequest {
                ssid: legacy_req.ssid.as_bytes().to_vec(),
                password: legacy_req.pass_phrase.as_bytes().to_vec(),
                responder,
            };
            future::ready(client.client.connect(req).map_err(|e| {
                eprintln!("Failed to start a connect transaction: {}", e);
                internal_error()
            }))
            .and_then(move |()| {
                receiver.map_err(|_e| {
                    eprintln!("Did not receive a connect result");
                    internal_error()
                })
            })
        })
        .map_ok(convert_connect_result)
        .unwrap_or_else(|e| e)
}

async fn disconnect(client: ClientRef) -> legacy::Error {
    let client = match client.get() {
        Ok(c) => c,
        Err(e) => return e,
    };
    let (responder, receiver) = oneshot::channel();
    if let Err(e) = client.client.disconnect(responder) {
        eprintln!("Failed to enqueue a disconnect command: {}", e);
        return internal_error();
    }
    match await!(receiver) {
        Ok(()) => success(),
        Err(_) => error_message("Request was canceled"),
    }
}

fn convert_connect_result(code: fidl_sme::ConnectResultCode) -> legacy::Error {
    match code {
        fidl_sme::ConnectResultCode::Success => success(),
        fidl_sme::ConnectResultCode::Canceled => error_message("Request was canceled"),
        fidl_sme::ConnectResultCode::BadCredentials => {
            error_message("Failed to join; bad credentials")
        }
        fidl_sme::ConnectResultCode::Failed => error_message("Failed to join"),
    }
}

fn status(client: &ClientRef) -> impl Future<Output = legacy::WlanStatus> {
    future::ready(client.get())
        .and_then(|client| {
            client.sme.status().map_err(|e| {
                eprintln!("Failed to query status: {}", e);
                internal_error()
            })
        })
        .map(|r| match r {
            Ok(status) => legacy::WlanStatus {
                error: success(),
                state: convert_state(&status),
                current_ap: status.connected_to.map(|bss| Box::new(convert_bss_info(bss.as_ref()))),
            },
            Err(error) => {
                legacy::WlanStatus { error, state: legacy::State::Unknown, current_ap: None }
            }
        })
}

fn convert_state(status: &fidl_sme::ClientStatusResponse) -> legacy::State {
    if status.connected_to.is_some() {
        legacy::State::Associated
    } else if !status.connecting_to_ssid.is_empty() {
        legacy::State::Joining
    } else {
        // There is no "idle" or "disconnected" state in the legacy API
        legacy::State::Querying
    }
}

fn stats(client: &ClientRef) -> impl Future<Output = legacy::WlanStats> {
    future::ready(client.get())
        .and_then(|client| {
            client.service.get_iface_stats(client.iface_id).map_err(|e| {
                eprintln!("Failed to query statistics: {}", e);
                internal_error()
            })
        })
        .map(|r| match r {
            Ok((zx::sys::ZX_OK, Some(iface_stats))) => {
                legacy::WlanStats { error: success(), stats: *iface_stats }
            }
            Ok((err_code, _)) => {
                eprintln!("GetIfaceStats returned error code {}", zx::Status::from_raw(err_code));
                legacy::WlanStats { error: internal_error(), stats: empty_stats() }
            }
            Err(error) => legacy::WlanStats { error, stats: empty_stats() },
        })
}

fn internal_error() -> legacy::Error {
    legacy::Error {
        code: legacy::ErrCode::Internal,
        description: "Internal error occurred".to_string(),
    }
}

fn success() -> legacy::Error {
    legacy::Error { code: legacy::ErrCode::Ok, description: String::new() }
}

fn not_supported() -> legacy::Error {
    legacy::Error { code: legacy::ErrCode::NotSupported, description: "Not supported".to_string() }
}

fn error_message(msg: &str) -> legacy::Error {
    legacy::Error { code: legacy::ErrCode::Internal, description: msg.to_string() }
}

fn empty_stats() -> fidl_wlan_stats::IfaceStats {
    fidl_wlan_stats::IfaceStats {
        dispatcher_stats: fidl_wlan_stats::DispatcherStats {
            any_packet: empty_packet_counter(),
            mgmt_frame: empty_packet_counter(),
            ctrl_frame: empty_packet_counter(),
            data_frame: empty_packet_counter(),
        },
        mlme_stats: None,
    }
}

fn empty_packet_counter() -> fidl_wlan_stats::PacketCounter {
    fidl_wlan_stats::PacketCounter {
        in_: empty_counter(),
        out: empty_counter(),
        drop: empty_counter(),
        in_bytes: empty_counter(),
        out_bytes: empty_counter(),
        drop_bytes: empty_counter(),
    }
}

fn empty_counter() -> fidl_wlan_stats::Counter {
    fidl_wlan_stats::Counter { count: 0, name: String::new() }
}
