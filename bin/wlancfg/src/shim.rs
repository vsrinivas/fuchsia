// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl::{self, endpoints2::create_endpoints};
use futures::prelude::*;
use legacy;
use fidl_wlan_stats;
use fidl_mlme;
use fidl_sme;
use std::sync::{Arc, Mutex};
use wlan_service;
use zx;

#[derive(Clone)]
pub struct Client {
    pub service: wlan_service::DeviceServiceProxy,
    pub sme: fidl_sme::ClientSmeProxy,
    pub iface_id: u16
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
            None => false
        };
        if same_id {
            *c = None;
        }
    }

    pub fn get(&self) -> Result<Client, legacy::Error>  {
        self.0.lock().unwrap().clone().ok_or_else(||
            legacy::Error {
                code: legacy::ErrCode::NotFound,
                description: "No wireless interface found".to_string(),
            })
    }
}

pub fn serve_legacy(requests: legacy::WlanRequestStream, client: ClientRef)
    -> impl Future<Item = (), Error = fidl::Error>
{
    many_futures!(WlanFut, [Scan, Connect, Disconnect, Status, StartBss, StopBss, Stats]);
    requests.for_each_concurrent(move |request| match request {
        legacy::WlanRequest::Scan { req, responder } => WlanFut::Scan(
            scan(&client, req)
                .then(move |r| match r {
                    Ok(mut r) => responder.send(&mut r),
                    Err(e) => e.never_into()
                })
        ),
        legacy::WlanRequest::Connect { req, responder } => WlanFut::Connect(
            connect(&client, req)
                .then(move |r| match r {
                    Ok(mut r) => responder.send(&mut r),
                    Err(e) => e.never_into()
                })
        ),
        legacy::WlanRequest::Disconnect { responder } => WlanFut::Disconnect({
            eprintln!("Disconnect() is not implemented");
            responder.send(&mut not_supported()).into_future()
        }),
        legacy::WlanRequest::Status { responder } => WlanFut::Status(
            status(&client)
                .then(move |r| match r {
                    Ok(mut r) => responder.send(&mut r),
                    Err(e) => e.never_into()
                })
        ),
        legacy::WlanRequest::StartBss { responder, .. } => WlanFut::StartBss({
            eprintln!("StartBss() is not implemented");
            responder.send(&mut not_supported()).into_future()
        }),
        legacy::WlanRequest::StopBss { responder } => WlanFut::StopBss({
            eprintln!("StopBss() is not implemented");
            responder.send(&mut not_supported()).into_future()
        }),
        legacy::WlanRequest::Stats { responder } => WlanFut::Stats(
            stats(&client)
                .then(move |r| match r {
                    Ok(mut r) => responder.send(&mut r),
                    Err(e) => e.never_into()
                })
        ),
    }).map(|_| ())
}

fn scan(client: &ClientRef, legacy_req: legacy::ScanRequest)
    -> impl Future<Item = legacy::ScanResult, Error = Never>
{
    client.get()
        .into_future()
        .and_then(move |client| {
            start_scan_txn(&client, legacy_req)
                .map_err(|e| {
                    eprintln!("Failed to start a scan transaction: {}", e);
                    internal_error()
                })
        })
        .and_then(|scan_txn| scan_txn.take_event_stream()
            .map_err(|e| {
                eprintln!("Error reading from scan transaction stream: {}", e);
                internal_error()
            })
            .fold((Vec::new(), false), |(mut old_aps, _done), event| {
                match event {
                    fidl_sme::ScanTransactionEvent::OnResult { aps } => {
                        old_aps.extend(aps);
                        Ok((old_aps, false))
                    },
                    fidl_sme::ScanTransactionEvent::OnFinished { } => Ok((old_aps, true)),
                    fidl_sme::ScanTransactionEvent::OnError { error } => Err(convert_scan_err(error))
                }
            }))
        .and_then(|(aps, done)| {
            if !done {
                eprintln!("Failed to fetch all results before the channel was closed");
                Err(internal_error())
            } else {
                Ok(aps.into_iter().map(|ess| convert_bss_info(ess.best_bss)).collect())
            }
        })
        .then(|r| match r {
            Ok(aps) => Ok(legacy::ScanResult { error: success(), aps: Some(aps) }),
            Err(error) => Ok(legacy::ScanResult { error, aps: None })
        })
}

fn start_scan_txn(client: &Client, legacy_req: legacy::ScanRequest)
    -> Result<fidl_sme::ScanTransactionProxy, fidl::Error>
{
    let (scan_txn, remote) = create_endpoints()?;
    let mut req = fidl_sme::ScanRequest {
        timeout: legacy_req.timeout
    };
    client.sme.scan(&mut req, remote)?;
    Ok(scan_txn)
}

fn convert_scan_err(error: fidl_sme::ScanError) -> legacy::Error {
    legacy::Error {
        code: match error.code {
            fidl_sme::ScanErrorCode::NotSupported
            => legacy::ErrCode::NotSupported
        },
        description: error.message
    }
}

fn convert_bss_info(bss: fidl_sme::BssInfo) -> legacy::Ap {
    legacy::Ap {
        bssid: bss.bssid.to_vec(),
        ssid: String::from_utf8_lossy(&bss.ssid).to_string(),
        rssi_dbm: bss.rx_dbm,
        is_secure: bss.protected,
        is_compatible: bss.compatible,
        chan: fidl_mlme::WlanChan {
            primary: bss.channel,
            secondary80: 0,
            cbw: fidl_mlme::Cbw::Cbw20
        }
    }
}

fn connect(client: &ClientRef, legacy_req: legacy::ConnectConfig)
    -> impl Future<Item = legacy::Error, Error = Never>
{
    client.get()
        .into_future()
        .and_then(|client| {
            start_connect_txn(&client, legacy_req)
                .map_err(|e| {
                    eprintln!("Failed to start a connect transaction: {}", e);
                    internal_error()
                })
        })
        .and_then(|txn| txn.take_event_stream()
            .map_err(|e| {
                eprintln!("Error reading from connect transaction stream: {}", e);
                internal_error()
            })
            .filter_map(|event| match event {
                fidl_sme::ConnectTransactionEvent::OnFinished { code } => Ok(Some(code)),
            })
            .next()
            .map_err(|(e, _stream)| e)
        )
        .and_then(|(code, _stream)| match code {
            None => {
                eprintln!("Connect transaction ended abruptly");
                Ok(internal_error())
            },
            Some(fidl_sme::ConnectResultCode::Success) => Ok(success()),
            Some(fidl_sme::ConnectResultCode::Canceled)
                => Ok(error_message("Request was canceled")),
            Some(fidl_sme::ConnectResultCode::Failed)
                => Ok(error_message("Failed to join"))
        })
        .recover(|e| e)
}

fn start_connect_txn(client: &Client, legacy_req: legacy::ConnectConfig)
    -> Result<fidl_sme::ConnectTransactionProxy, fidl::Error>
{
    let (connect_txn, remote) = create_endpoints()?;
    let mut req = fidl_sme::ConnectRequest {
        ssid: legacy_req.ssid.as_bytes().to_vec(),
    };
    client.sme.connect(&mut req, Some(remote))?;
    Ok(connect_txn)
}

fn status(client: &ClientRef)
    -> impl Future<Item = legacy::WlanStatus, Error = Never>
{
    client.get()
        .into_future()
        .and_then(|client| {
            client.sme.status()
                .map_err(|e| {
                    eprintln!("Failed to query status: {}", e);
                    internal_error()
                })
        })
        .then(|r| Ok(match r {
            Ok(status) => legacy::WlanStatus {
                error: success(),
                state: convert_state(&status),
                current_ap: status.connected_to.map(|bss| Box::new(convert_bss_info(*bss))),
            },
            Err(error) => legacy::WlanStatus {
                error,
                state: legacy::State::Unknown,
                current_ap: None
            }
        }))
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

fn stats(client: &ClientRef)
    -> impl Future<Item = legacy::WlanStats, Error = Never>
{
    client.get()
        .into_future()
        .and_then(|client| {
            client.service.get_iface_stats(client.iface_id)
                .map_err(|e| {
                    eprintln!("Failed to query statistics: {}", e);
                    internal_error()
                })
        })
        .then(|r| Ok(match r {
            Ok((zx::sys::ZX_OK, Some(iface_stats))) => legacy::WlanStats {
                error: success(),
                stats: *iface_stats,
            },
            Ok((err_code, _)) => {
                eprintln!("GetIfaceStats returned error code {}", zx::Status::from_raw(err_code));
                legacy::WlanStats {
                    error: internal_error(),
                    stats: empty_stats(),
                }
            },
            Err(error) => legacy::WlanStats {
                error,
                stats: empty_stats(),
            }
        }))
}

fn internal_error() -> legacy::Error {
    legacy::Error {
        code: legacy::ErrCode::Internal,
        description: "Internal error occurred".to_string(),
    }
}

fn success() -> legacy::Error {
    legacy::Error {
        code: legacy::ErrCode::Ok,
        description: String::new()
    }
}

fn not_supported() -> legacy::Error {
    legacy::Error {
        code: legacy::ErrCode::NotSupported,
        description: "Not supported".to_string()
    }
}

fn error_message(msg: &str) -> legacy::Error {
    legacy::Error {
        code: legacy::ErrCode::Internal,
        description: msg.to_string(),
    }
}

fn empty_stats() -> fidl_wlan_stats::IfaceStats {
    fidl_wlan_stats::IfaceStats {
        dispatcher_stats: fidl_wlan_stats::DispatcherStats {
            any_packet: empty_packet_counter(),
            mgmt_frame: empty_packet_counter(),
            ctrl_frame: empty_packet_counter(),
            data_frame: empty_packet_counter(),
        },
        mlme_stats: None
    }
}

fn empty_packet_counter() -> fidl_wlan_stats::PacketCounter {
    fidl_wlan_stats::PacketCounter {
        in_: empty_counter(),
        out: empty_counter(),
        drop: empty_counter(),
    }
}

fn empty_counter() -> fidl_wlan_stats::Counter {
    fidl_wlan_stats::Counter {
        count: 0,
        name: String::new(),
    }
}

