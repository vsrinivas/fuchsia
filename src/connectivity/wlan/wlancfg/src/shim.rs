// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{client, config_manager::SavedNetworksManager, known_ess_store::KnownEssStore};

use {
    fidl::{self, endpoints::create_proxy},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_device_service as wlan_service,
    fidl_fuchsia_wlan_service as legacy, fidl_fuchsia_wlan_sme as fidl_sme,
    fidl_fuchsia_wlan_stats as fidl_wlan_stats, fuchsia_zircon as zx,
    futures::{channel::oneshot, prelude::*},
    itertools::Itertools,
    log::info,
    std::{
        cmp::Ordering,
        collections::HashMap,
        sync::{Arc, Mutex},
    },
};

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
    saved_networks: Arc<SavedNetworksManager>,
) -> Result<(), fidl::Error> {
    requests
        .try_for_each_concurrent(MAX_CONCURRENT_WLAN_REQUESTS, |req| {
            handle_request(&client, req, Arc::clone(&ess_store), Arc::clone(&saved_networks))
        })
        .await
}

async fn handle_request(
    client: &ClientRef,
    req: legacy::WlanRequest,
    ess_store: Arc<KnownEssStore>,
    saved_networks: Arc<SavedNetworksManager>,
) -> Result<(), fidl::Error> {
    match req {
        legacy::WlanRequest::Scan { req, responder } => {
            let mut r = scan(client, req).await;
            responder.send(&mut r)
        }
        legacy::WlanRequest::Connect { req, responder } => {
            let mut r = connect(&client, req).await;
            responder.send(&mut r)
        }
        legacy::WlanRequest::Disconnect { responder } => {
            let mut r = disconnect(client.clone()).await;
            responder.send(&mut r)
        }
        legacy::WlanRequest::Status { responder } => {
            let mut r = status(&client).await;
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
            let mut r = stats(client).await;
            responder.send(&mut r)
        }
        legacy::WlanRequest::ClearSavedNetworks { responder } => {
            info!("Clearing all saved networks.");
            if let Err(e) = ess_store.clear() {
                eprintln!("Error clearing known ESS: {}", e);
            }
            if let Err(e) = saved_networks.clear() {
                eprintln!("Error clearing network configs: {}", e);
            }
            responder.send()
        }
    }
}

pub fn clone_bss_info(bss: &fidl_sme::BssInfo) -> fidl_sme::BssInfo {
    fidl_sme::BssInfo {
        bssid: bss.bssid.clone(),
        ssid: bss.ssid.clone(),
        rx_dbm: bss.rx_dbm,
        channel: bss.channel,
        protection: bss.protection,
        compatible: bss.compatible,
    }
}

/// Compares two BSS based on
/// (1) their compatibility
/// (2) their security protocol
/// (3) their Beacon's RSSI
pub fn compare_bss(left: &fidl_sme::BssInfo, right: &fidl_sme::BssInfo) -> Ordering {
    left.compatible
        .cmp(&right.compatible)
        .then(left.protection.cmp(&right.protection))
        .then(left.rx_dbm.cmp(&right.rx_dbm))
}

/// Returns the 'best' BSS from a given BSS list. The 'best' BSS is determined by comparing
/// all BSS with `compare_bss(BssDescription, BssDescription)`.
pub fn get_best_bss<'a>(bss_list: &'a Vec<fidl_sme::BssInfo>) -> Option<&'a fidl_sme::BssInfo> {
    bss_list.iter().max_by(|x, y| compare_bss(x, y))
}

async fn scan(client: &ClientRef, legacy_req: legacy::ScanRequest) -> legacy::ScanResult {
    let r = async move {
        let client = client.get()?;
        let scan_txn = start_scan_txn(&client, legacy_req).map_err(|e| {
            eprintln!("Failed to start a scan transaction: {}", e);
            internal_error()
        })?;

        let mut evt_stream = scan_txn.take_event_stream();
        let mut aps = vec![];
        let mut done = false;

        while let Some(event) = evt_stream.try_next().await.map_err(|e| {
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

        let mut bss_by_ssid: HashMap<Vec<u8>, Vec<fidl_sme::BssInfo>> = HashMap::new();
        for bss in aps.iter() {
            bss_by_ssid.entry(bss.ssid.clone()).or_insert(vec![]).push(clone_bss_info(&bss));
        }

        Ok(bss_by_ssid
            .values()
            .filter_map(get_best_bss)
            .map(convert_bss_info)
            .sorted_by(|a, b| a.ssid.cmp(&b.ssid))
            .collect())
    }
    .await;

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
        is_secure: bss.protection != fidl_sme::Protection::Open,
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
    match receiver.await {
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

#[cfg(test)]
mod tests {
    use {
        super::*, crate::known_ess_store::KnownEss, fidl_fuchsia_wlan_policy as fidl_policy,
        fuchsia_async as fasync, futures::task::Poll, pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

    #[test]
    fn test_convert_bss_to_legacy_ap() {
        let bss = fidl_sme::BssInfo {
            bssid: [0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f],
            ssid: b"Foo".to_vec(),
            rx_dbm: 20,
            channel: 1,
            protection: fidl_sme::Protection::Wpa2Personal,
            compatible: true,
        };
        let ap = legacy::Ap {
            bssid: vec![0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f],
            ssid: "Foo".to_string(),
            rssi_dbm: 20,
            is_secure: true,
            is_compatible: true,
            chan: fidl_common::WlanChan {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::Cbw::Cbw20,
            },
        };

        assert_eq!(convert_bss_info(&bss), ap);

        let bss = fidl_sme::BssInfo { protection: fidl_sme::Protection::Open, ..bss };
        let ap = legacy::Ap { is_secure: false, ..ap };

        assert_eq!(convert_bss_info(&bss), ap);
    }

    #[test]
    fn get_best_bss_empty_list() {
        assert!(get_best_bss(&vec![]).is_none());
    }

    #[test]
    fn get_best_bss_compatible() {
        let bss2 = bss(-20, false, fidl_sme::Protection::Wpa2Wpa3Personal);
        let bss1 = bss(-60, true, fidl_sme::Protection::Wpa2Personal);
        let bss_list = vec![bss1, bss2];

        assert_eq!(get_best_bss(&bss_list), Some(&bss_list[0])); //diff compatible
    }

    #[test]
    fn get_best_bss_protection() {
        let bss1 = bss(-60, true, fidl_sme::Protection::Wpa2Personal);
        let bss2 = bss(-40, true, fidl_sme::Protection::Wep);
        let bss_list = vec![bss1, bss2];

        assert_eq!(get_best_bss(&bss_list), Some(&bss_list[0])); //diff protection
    }

    #[test]
    fn get_best_bss_rssi() {
        let bss1 = bss(-10, true, fidl_sme::Protection::Wpa2Personal);
        let bss2 = bss(-20, true, fidl_sme::Protection::Wpa2Personal);
        let bss_list = vec![bss1, bss2];

        assert_eq!(get_best_bss(&bss_list), Some(&bss_list[0])); //diff rssi
    }

    fn bss(rx_dbm: i8, compatible: bool, protection: fidl_sme::Protection) -> fidl_sme::BssInfo {
        fidl_sme::BssInfo {
            bssid: [0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f],
            ssid: b"Foo".to_vec(),
            rx_dbm,
            channel: 1,
            protection,
            compatible,
        }
    }
    #[test]
    fn clear_saved_networks_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let temp_path = temp_dir.path();
        let saved_networks = Arc::new(
            SavedNetworksManager::new_with_paths(
                temp_path.join("network_store.json").to_path_buf(),
            )
            .expect("Failed to make saved networks manager"),
        );
        let ess_store = Arc::new(
            KnownEssStore::new_with_paths(
                temp_path.join("store.json").to_path_buf(),
                temp_path.join("store.json.temp"),
            )
            .expect("Failed to create a KnownEssStore"),
        );

        let (wlan_proxy, requests) =
            create_proxy::<legacy::WlanMarker>().expect("failed to create WlanRequest proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        // Begin handling legacy requests.
        let fut = serve_legacy(
            requests,
            ClientRef::new(),
            Arc::clone(&ess_store),
            Arc::clone(&saved_networks),
        )
        .unwrap_or_else(|e| panic!("failed to serve legacy requests: {}", e));
        fasync::spawn(fut);

        // Save the networks directly without going through the legacy API.
        ess_store
            .store(b"foo".to_vec(), KnownEss { password: b"qwertyuio".to_vec() })
            .expect("Failed to store to ess store");
        saved_networks
            .store(b"foo".to_vec(), fidl_policy::Credential::Password(b"qwertyuio".to_vec()))
            .expect("Failed to save network");

        // Send the request to clear saved networks.
        let clear_fut = wlan_proxy.clear_saved_networks();
        pin_mut!(clear_fut);

        // Process request to clear saved networks.
        assert_variant!(exec.run_until_stalled(&mut clear_fut), Poll::Ready(Ok(_)));

        // There should be no networks saved.
        assert_eq!(0, saved_networks.known_network_count());
        assert_eq!(0, ess_store.known_network_count());
    }
}
