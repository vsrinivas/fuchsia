// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        client::state_machine as client_fsm,
        config_management::{Credential, NetworkIdentifier, SavedNetworksManager, SecurityType},
        mode_management::iface_manager_api::IfaceManagerApi,
    },
    fidl, fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_device_service as wlan_service, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_service as legacy, fidl_fuchsia_wlan_sme as fidl_sme,
    fidl_fuchsia_wlan_stats as fidl_wlan_stats, fuchsia_async, fuchsia_zircon as zx,
    futures::{lock::Mutex as FutureMutex, prelude::*, select},
    itertools::Itertools,
    log::{debug, error, info},
    std::{
        cmp::Ordering,
        collections::HashMap,
        sync::{Arc, Mutex},
    },
};

#[derive(Clone)]
pub(crate) struct Iface {
    pub service: wlan_service::DeviceServiceProxy,
    pub iface_manager: Arc<FutureMutex<dyn IfaceManagerApi + Send>>,
    pub sme: fidl_sme::ClientSmeProxy,
    pub iface_id: u16,
}

#[derive(Clone)]
pub(crate) struct IfaceRef(Arc<Mutex<Option<Iface>>>);
impl IfaceRef {
    pub fn new() -> Self {
        IfaceRef(Arc::new(Mutex::new(None)))
    }
    pub fn set_if_empty(&self, iface: Iface) {
        let mut c = self.0.lock().unwrap();
        if c.is_none() {
            *c = Some(iface);
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
    pub fn get(&self) -> Result<Iface, legacy::Error> {
        self.0.lock().unwrap().clone().ok_or_else(|| legacy::Error {
            code: legacy::ErrCode::NotFound,
            description: "No wireless interface found".to_string(),
        })
    }
}

const MAX_CONCURRENT_WLAN_REQUESTS: usize = 1000;

pub(crate) async fn serve_legacy(
    requests: legacy::WlanRequestStream,
    iface: IfaceRef,
    saved_networks: Arc<SavedNetworksManager>,
) -> Result<(), fidl::Error> {
    requests
        .try_for_each_concurrent(MAX_CONCURRENT_WLAN_REQUESTS, |req| {
            handle_request(iface.clone(), req, Arc::clone(&saved_networks))
        })
        .await
}

async fn handle_request(
    iface: IfaceRef,
    req: legacy::WlanRequest,
    saved_networks: Arc<SavedNetworksManager>,
) -> Result<(), fidl::Error> {
    match req {
        legacy::WlanRequest::Scan { req, responder } => {
            info!("Legacy WLAN API used for scan request");
            let mut r = scan(iface, req).await;
            responder.send(&mut r)
        }
        legacy::WlanRequest::Connect { req, responder } => {
            info!("Legacy WLAN API used for connect request");
            let mut r = connect(iface, saved_networks, req).await;
            responder.send(&mut r)
        }
        legacy::WlanRequest::Disconnect { responder } => {
            info!("Legacy WLAN API used for disconnect request");
            let mut r = disconnect(iface.clone()).await;
            responder.send(&mut r)
        }
        legacy::WlanRequest::Status { responder } => {
            debug!("Legacy WLAN API used for status request");
            let mut r = status(&iface).await;
            responder.send(&mut r)
        }
        legacy::WlanRequest::StartBss { responder, .. } => {
            error!("StartBss() is not implemented");
            responder.send(&mut not_supported())
        }
        legacy::WlanRequest::StopBss { responder } => {
            error!("StopBss() is not implemented");
            responder.send(&mut not_supported())
        }
        legacy::WlanRequest::Stats { responder } => {
            debug!("Legacy WLAN API used for stats request");
            let mut r = stats(&iface).await;
            responder.send(&mut r)
        }
        legacy::WlanRequest::ClearSavedNetworks { responder } => {
            info!("Legacy WLAN API used to clear all saved networks");
            if let Err(e) = saved_networks.clear().await {
                error!("Error clearing network configs: {}", e);
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
        snr_db: bss.snr_db,
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

async fn scan(iface: IfaceRef, legacy_req: legacy::ScanRequest) -> legacy::ScanResult {
    let r = async move {
        let iface = iface.get()?;
        let mut iface_manager = iface.iface_manager.lock().await;
        let scan_txn = iface_manager
            .scan(legacy_req.timeout, fidl_common::ScanType::Passive)
            .map_err(|e| {
                error!("Failed to start a scan transaction: {}", e);
                internal_error()
            })
            .await?;
        let mut evt_stream = scan_txn.take_event_stream();
        let mut aps = vec![];
        let mut done = false;

        while let Some(event) = evt_stream.try_next().await.map_err(|e| {
            error!("Error reading from scan transaction stream: {}", e);
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
            error!("Failed to fetch all results before the channel was closed");
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

fn legacy_config_to_network_id(legacy_req: &legacy::ConnectConfig) -> NetworkIdentifier {
    let security_type = match legacy_req.pass_phrase.len() {
        0 => SecurityType::None,
        _ => SecurityType::Wpa2,
    };
    let ssid = legacy_req.ssid.clone().as_bytes().to_vec();

    NetworkIdentifier::new(ssid, security_type)
}

fn legacy_config_to_policy_credential(legacy_req: &legacy::ConnectConfig) -> Credential {
    match legacy_req.pass_phrase.len() {
        0 => Credential::None,
        _ => Credential::Password(legacy_req.pass_phrase.clone().as_bytes().to_vec()),
    }
}

async fn connect(
    iface: IfaceRef,
    saved_networks: Arc<SavedNetworksManager>,
    legacy_req: legacy::ConnectConfig,
) -> legacy::Error {
    let network_id = legacy_config_to_network_id(&legacy_req);
    let credential = legacy_config_to_policy_credential(&legacy_req);
    info!("Automatically (re-)saving network used in legacy connect request");
    match saved_networks.store(network_id.clone(), credential.clone()).await {
        Ok(()) => {}
        Err(e) => {
            error!("failed to store connect config: {:?}", e);
            return internal_error();
        }
    }

    let iface = match iface.get() {
        Ok(c) => c,
        Err(e) => return e,
    };

    let connect_req = client_fsm::ConnectRequest {
        network: fidl_policy::NetworkIdentifier::from(network_id),
        credential,
    };

    // Get the state machine to begin connecting to the desired network.
    let mut iface_manager = iface.iface_manager.lock().await;
    match iface_manager.connect(connect_req).await {
        Ok(receiver) => match receiver.await {
            Ok(()) => {}
            Err(e) => return error_message(&format!("connect failed: {}", e)),
        },
        Err(e) => return error_message(&e.to_string()),
    }
    drop(iface_manager);

    // Poll the interface and wait for it to connect.  Time out after 20 seconds.
    let timeout = zx::Duration::from_seconds(20);
    let mut timer = fuchsia_async::Interval::new(timeout);
    let mut status_fut = iface.sme.status();
    loop {
        select! {
            result = status_fut => {
                match result {
                    Ok(status) => {
                        if status.connected_to.is_some() {
                            return success();
                        }
                        status_fut = iface.sme.clone().status();
                    },
                    Err(e) => {
                        return error_message(&format!("failed query connection status: {}", e));
                    }
                };
            },
            () = timer.select_next_some() => return internal_error(),
        }
    }
}

async fn disconnect(iface: IfaceRef) -> legacy::Error {
    let iface = match iface.get() {
        Ok(c) => c,
        Err(e) => return e,
    };
    let mut iface_manager = iface.iface_manager.lock().await;
    match iface_manager.stop_client_connections().await {
        Ok(()) => {}
        Err(e) => return error_message(&e.to_string()),
    }
    match iface_manager.start_client_connections().await {
        Ok(()) => success(),
        Err(e) => error_message(&e.to_string()),
    }
}

fn status(iface: &IfaceRef) -> impl Future<Output = legacy::WlanStatus> {
    future::ready(iface.get())
        .and_then(|iface| {
            iface.sme.status().map_err(|e| {
                error!("Failed to query status: {}", e);
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

fn stats(iface: &IfaceRef) -> impl Future<Output = legacy::WlanStats> {
    future::ready(iface.get())
        .and_then(|iface| {
            iface.service.get_iface_stats(iface.iface_id).map_err(|e| {
                error!("Failed to query statistics: {}", e);
                internal_error()
            })
        })
        .map(|r| match r {
            Ok((zx::sys::ZX_OK, Some(iface_stats))) => {
                legacy::WlanStats { error: success(), stats: *iface_stats }
            }
            Ok((err_code, _)) => {
                error!("GetIfaceStats returned error code {}", zx::Status::from_raw(err_code));
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
        super::*,
        crate::{access_point::state_machine as ap_fsm, util::logger::set_logger_for_test},
        async_trait::async_trait,
        fidl::endpoints::create_proxy,
        fuchsia_async as fasync,
        futures::{channel::oneshot, lock::Mutex as FutureMutex, stream::StreamFuture, task::Poll},
        pin_utils::pin_mut,
        rand::{distributions::Alphanumeric, thread_rng, Rng},
        tempfile::TempDir,
        wlan_common::assert_variant,
    };

    #[test]
    fn test_convert_bss_to_legacy_ap() {
        let bss = fidl_sme::BssInfo {
            bssid: [0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f],
            ssid: b"Foo".to_vec(),
            rx_dbm: 20,
            snr_db: 0,
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
            snr_db: 0,
            channel: 1,
            protection,
            compatible,
        }
    }

    #[test]
    fn clear_saved_networks_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join(rand_string());
        let tmp_path = temp_dir.path().join(rand_string());

        let (saved_networks, mut stash_server) =
            exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server(path, tmp_path));
        let saved_networks = Arc::new(saved_networks);

        let (wlan_proxy, requests) =
            create_proxy::<legacy::WlanMarker>().expect("failed to create WlanRequest proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        // Begin handling legacy requests.
        let fut = serve_legacy(requests, IfaceRef::new(), Arc::clone(&saved_networks))
            .unwrap_or_else(|e| panic!("failed to serve legacy requests: {}", e));
        fasync::Task::spawn(fut).detach();

        // Save the network directly without going through the legacy API.
        let network_id = NetworkIdentifier::new(b"foo".to_vec(), SecurityType::Wpa2);
        let save_fut =
            saved_networks.store(network_id, Credential::Password(b"qwertyuio".to_vec()));
        pin_mut!(save_fut);
        assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Pending);
        // Process stash write for saved network
        process_stash_write(&mut exec, &mut stash_server);
        assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Ready(Ok(())));

        // Send the request to clear saved networks.
        let clear_fut = wlan_proxy.clear_saved_networks();
        pin_mut!(clear_fut);
        // Process stash remove for saved network
        assert_variant!(exec.run_until_stalled(&mut clear_fut), Poll::Pending);
        process_stash_remove(&mut exec, &mut stash_server);

        // Process request to clear saved networks.
        assert_variant!(exec.run_until_stalled(&mut clear_fut), Poll::Ready(Ok(_)));

        // There should be no networks saved.
        assert_eq!(0, exec.run_singlethreaded(saved_networks.known_network_count()));
    }

    pub static TEST_SSID: &str = "test_ssid";
    pub static TEST_PASSWORD: &str = "test_password";
    pub static TEST_BSSID: &str = "00:11:22:33:44:55";
    const TEST_SCAN_INTERVAL: u8 = 0;

    fn poll_sme_req(
        exec: &mut fasync::Executor,
        next_sme_req: &mut StreamFuture<fidl_sme::ClientSmeRequestStream>,
    ) -> Poll<fidl_sme::ClientSmeRequest> {
        exec.run_until_stalled(next_sme_req).map(|(req, stream)| {
            *next_sme_req = stream.into_future();
            req.expect("did not expect the SME request stream to end")
                .expect("error polling SME request stream")
        })
    }

    struct FakeIfaceManager {
        pub sme_proxy: fidl_fuchsia_wlan_sme::ClientSmeProxy,
        pub connect_succeeds: bool,
        pub disconnect_succeeds: bool,
        pub scan_succeeds: bool,
    }

    impl FakeIfaceManager {
        pub fn new(
            proxy: fidl_fuchsia_wlan_sme::ClientSmeProxy,
            connect_succeeds: bool,
            disconnect_succeeds: bool,
            scan_succeeds: bool,
        ) -> Self {
            FakeIfaceManager {
                sme_proxy: proxy,
                connect_succeeds,
                disconnect_succeeds,
                scan_succeeds,
            }
        }
    }

    #[async_trait]
    impl IfaceManagerApi for FakeIfaceManager {
        async fn disconnect(
            &mut self,
            _network_id: fidl_fuchsia_wlan_policy::NetworkIdentifier,
        ) -> Result<(), anyhow::Error> {
            if !self.disconnect_succeeds {
                return Err(anyhow::format_err!("failing to disconnect"));
            }
            Ok(())
        }

        async fn connect(
            &mut self,
            _connect_req: client_fsm::ConnectRequest,
        ) -> Result<oneshot::Receiver<()>, anyhow::Error> {
            if !self.connect_succeeds {
                return Err(anyhow::format_err!("failing to connect"));
            }

            let (responder, receiver) = oneshot::channel();
            let _ = responder.send(());
            Ok(receiver)
        }

        async fn record_idle_client(&mut self, _iface_id: u16) -> Result<(), anyhow::Error> {
            unimplemented!()
        }

        async fn has_idle_client(&mut self) -> Result<bool, anyhow::Error> {
            unimplemented!()
        }

        async fn handle_added_iface(&mut self, _iface_id: u16) -> Result<(), anyhow::Error> {
            unimplemented!()
        }

        async fn handle_removed_iface(&mut self, _iface_id: u16) -> Result<(), anyhow::Error> {
            unimplemented!()
        }

        async fn scan(
            &mut self,
            timeout: u8,
            scan_type: fidl_fuchsia_wlan_common::ScanType,
        ) -> Result<fidl_fuchsia_wlan_sme::ScanTransactionProxy, anyhow::Error> {
            if !self.scan_succeeds {
                return Err(anyhow::format_err!("failing to scan"));
            }

            let (local, remote) = fidl::endpoints::create_proxy()?;
            let mut request =
                fidl_fuchsia_wlan_sme::ScanRequest { timeout: timeout, scan_type: scan_type };
            let _ = self.sme_proxy.scan(&mut request, remote);
            Ok(local)
        }

        async fn stop_client_connections(&mut self) -> Result<(), anyhow::Error> {
            if !self.disconnect_succeeds {
                return Err(anyhow::format_err!("failing to disconnect"));
            }
            Ok(())
        }

        async fn start_client_connections(&mut self) -> Result<(), anyhow::Error> {
            Ok(())
        }

        async fn start_ap(
            &mut self,
            _config: ap_fsm::ApConfig,
        ) -> Result<oneshot::Receiver<fidl_fuchsia_wlan_sme::StartApResultCode>, anyhow::Error>
        {
            unimplemented!()
        }

        async fn stop_ap(
            &mut self,
            _ssid: Vec<u8>,
            _password: Vec<u8>,
        ) -> Result<(), anyhow::Error> {
            unimplemented!()
        }

        async fn stop_all_aps(&mut self) -> Result<(), anyhow::Error> {
            unimplemented!()
        }
    }

    struct TestValues {
        iface: IfaceRef,
        _dev_svc_stream: wlan_service::DeviceServiceRequestStream,
        sme_stream: fidl_sme::ClientSmeRequestStream,
        _temp_dir: TempDir,
        saved_networks: Arc<SavedNetworksManager>,
        legacy_proxy: legacy::WlanProxy,
        legacy_stream: legacy::WlanRequestStream,
        stash_server: fidl_fuchsia_stash::StoreAccessorRequestStream,
    }

    fn rand_string() -> String {
        thread_rng().sample_iter(&Alphanumeric).take(20).collect()
    }

    fn test_setup(
        exec: &mut fasync::Executor,
        connect_succeeds: bool,
        disconnect_succeeds: bool,
        scan_succeeds: bool,
    ) -> TestValues {
        set_logger_for_test();
        let temp_dir = TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join(rand_string());
        let tmp_path = temp_dir.path().join(rand_string());

        let (saved_networks, stash_server) =
            exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server(path, tmp_path));
        let saved_networks = Arc::new(saved_networks);

        let (sme_proxy, sme_server) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("failed to create an sme channel");
        let sme_stream = sme_server.into_stream().expect("failed to create SME stream");
        let (dev_svc_proxy, dev_svc_server) = create_proxy::<wlan_service::DeviceServiceMarker>()
            .expect("failed to create an sme channel");
        let _dev_svc_stream =
            dev_svc_server.into_stream().expect("failed to create device service stream");

        let (legacy_proxy, requests) =
            create_proxy::<legacy::WlanMarker>().expect("failed to create WlanRequest proxy");
        let legacy_stream = requests.into_stream().expect("failed to create legacy request stream");

        let iface_manager = Arc::new(FutureMutex::new(FakeIfaceManager::new(
            sme_proxy.clone(),
            connect_succeeds,
            disconnect_succeeds,
            scan_succeeds,
        )));
        let iface = IfaceRef(Arc::new(Mutex::new(Some(Iface {
            service: dev_svc_proxy,
            iface_manager,
            sme: sme_proxy.clone(),
            iface_id: 0,
        }))));

        TestValues {
            iface,
            _dev_svc_stream,
            sme_stream,
            _temp_dir: temp_dir,
            saved_networks,
            legacy_proxy,
            legacy_stream,
            stash_server,
        }
    }

    /// Move stash requests forward so that a save request can progress.
    fn process_stash_write(
        mut exec: &mut fasync::Executor,
        mut stash_server: &mut fidl_fuchsia_stash::StoreAccessorRequestStream,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut stash_server.try_next()),
            Poll::Ready(Ok(Some(fidl_fuchsia_stash::StoreAccessorRequest::SetValue{..})))
        );
        process_stash_flush(&mut exec, &mut stash_server);
    }

    /// Move stash requests forward so that a remove request can progress.
    fn process_stash_remove(
        mut exec: &mut fasync::Executor,
        mut stash_server: &mut fidl_fuchsia_stash::StoreAccessorRequestStream,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut stash_server.try_next()),
            Poll::Ready(Ok(Some(fidl_fuchsia_stash::StoreAccessorRequest::DeletePrefix{..})))
        );
        process_stash_flush(&mut exec, &mut stash_server);
    }

    fn process_stash_flush(
        exec: &mut fasync::Executor,
        stash_server: &mut fidl_fuchsia_stash::StoreAccessorRequestStream,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut stash_server.try_next()),
            Poll::Ready(Ok(Some(fidl_fuchsia_stash::StoreAccessorRequest::Flush{responder}))) => {
                responder.send(&mut Ok(())).expect("failed to send stash response");
            }
        );
    }

    #[test]
    fn connect_succeeds() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec, true, true, true);
        // Start the legacy service.
        let serve_fut =
            serve_legacy(test_values.legacy_stream, test_values.iface, test_values.saved_networks);
        pin_mut!(serve_fut);

        // Make a connect request.
        let mut config = legacy::ConnectConfig {
            ssid: TEST_SSID.to_string(),
            pass_phrase: TEST_PASSWORD.to_string(),
            scan_interval: TEST_SCAN_INTERVAL,
            bssid: TEST_BSSID.to_string(),
        };
        let connect_fut = test_values.legacy_proxy.connect(&mut config);
        pin_mut!(connect_fut);
        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Pending);

        // Progress the service.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        // Process stash write for saved network
        process_stash_write(&mut exec, &mut test_values.stash_server);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Wait for a status request and send back a notification that the client is connected.
        let sme_fut = test_values.sme_stream.into_future();
        pin_mut!(sme_fut);
        let sme_response = poll_sme_req(&mut exec, &mut sme_fut);
        assert_variant!(
            sme_response,
            Poll::Ready(fidl_sme::ClientSmeRequest::Status{ responder }) => {
                responder.send(&mut fidl_sme::ClientStatusResponse{
                    connecting_to_ssid: vec![],
                    connected_to: Some(Box::new(fidl_sme::BssInfo{
                        bssid: [0, 0, 0, 0, 0, 0],
                        ssid: TEST_SSID.as_bytes().to_vec(),
                        rx_dbm: i8::MAX,
                        snr_db: i8::MAX,
                        channel: u8::MAX,
                        protection: fidl_sme::Protection::Unknown,
                        compatible: true,
                    }))
                }).expect("could not send sme response");
            }
        );

        // Run the connect future to completion
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Ok(legacy::Error {
                code: legacy::ErrCode::Ok,
                ..
            }))
        );
    }

    #[test]
    fn connect_timeout() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec, true, true, true);

        // Start the legacy service.
        let serve_fut =
            serve_legacy(test_values.legacy_stream, test_values.iface, test_values.saved_networks);
        pin_mut!(serve_fut);

        // Make a connect request.
        let mut config = legacy::ConnectConfig {
            ssid: TEST_SSID.to_string(),
            pass_phrase: TEST_PASSWORD.to_string(),
            scan_interval: TEST_SCAN_INTERVAL,
            bssid: TEST_BSSID.to_string(),
        };
        let connect_fut = test_values.legacy_proxy.connect(&mut config);
        pin_mut!(connect_fut);
        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Pending);

        // Progress the service.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        // Process stash write for saved network
        process_stash_write(&mut exec, &mut test_values.stash_server);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Run into the timeout.
        assert_variant!(exec.wake_next_timer(), Some(_));

        // Run the connect future to completion
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Ok(legacy::Error {
                code: legacy::ErrCode::Internal,
                ..
            }))
        );
    }

    #[test]
    fn connect_fails() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec, false, true, true);

        // Start the legacy service.
        let serve_fut =
            serve_legacy(test_values.legacy_stream, test_values.iface, test_values.saved_networks);
        pin_mut!(serve_fut);

        // Make a connect request.
        let mut config = legacy::ConnectConfig {
            ssid: TEST_SSID.to_string(),
            pass_phrase: TEST_PASSWORD.to_string(),
            scan_interval: TEST_SCAN_INTERVAL,
            bssid: TEST_BSSID.to_string(),
        };
        let connect_fut = test_values.legacy_proxy.connect(&mut config);
        pin_mut!(connect_fut);
        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Pending);

        // Progress the service.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        // Process stash write for saved network
        process_stash_write(&mut exec, &mut test_values.stash_server);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Run the connect future to completion
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Ok(legacy::Error {
                code: legacy::ErrCode::Internal,
                ..
            }))
        );
    }

    #[test]
    fn disconnect_succeeds() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec, true, true, true);

        // Start the legacy service.
        let serve_fut =
            serve_legacy(test_values.legacy_stream, test_values.iface, test_values.saved_networks);
        pin_mut!(serve_fut);

        // Make a disconnect request.
        let disconnect_fut = test_values.legacy_proxy.disconnect();
        pin_mut!(disconnect_fut);
        assert_variant!(exec.run_until_stalled(&mut disconnect_fut), Poll::Pending);

        // Progress the service.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Run the disconnect future to completion
        assert_variant!(
            exec.run_until_stalled(&mut disconnect_fut),
            Poll::Ready(Ok(legacy::Error {
                code: legacy::ErrCode::Ok,
                ..
            }))
        );
    }

    #[test]
    fn disconnect_fails() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec, true, false, true);

        // Start the legacy service.
        let serve_fut =
            serve_legacy(test_values.legacy_stream, test_values.iface, test_values.saved_networks);
        pin_mut!(serve_fut);

        // Make a disconnect request.
        let disconnect_fut = test_values.legacy_proxy.disconnect();
        pin_mut!(disconnect_fut);
        assert_variant!(exec.run_until_stalled(&mut disconnect_fut), Poll::Pending);

        // Progress the service.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Run the disconnect future to completion
        assert_variant!(
            exec.run_until_stalled(&mut disconnect_fut),
            Poll::Ready(Ok(legacy::Error {
                code: legacy::ErrCode::Internal,
                ..
            }))
        );
    }

    #[test]
    fn scan_succeeds() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec, true, true, true);

        // Start the legacy service.
        let serve_fut =
            serve_legacy(test_values.legacy_stream, test_values.iface, test_values.saved_networks);
        pin_mut!(serve_fut);

        // Make a scan request.
        let mut scan_request = legacy::ScanRequest { timeout: TEST_SCAN_INTERVAL };
        let scan_fut = test_values.legacy_proxy.scan(&mut scan_request);
        pin_mut!(scan_fut);
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Run the service.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Check the SME stream for a scan request and send a result and a finished message.
        let sme_fut = test_values.sme_stream.into_future();
        pin_mut!(sme_fut);
        let sme_response = poll_sme_req(&mut exec, &mut sme_fut);
        let scan_result = fidl_sme::BssInfo {
            bssid: [0; 6],
            ssid: TEST_SSID.as_bytes().to_vec(),
            rx_dbm: i8::MAX,
            snr_db: i8::MAX,
            channel: u8::MAX,
            protection: fidl_sme::Protection::Unknown,
            compatible: true,
        };
        assert_variant!(
            sme_response,
            Poll::Ready(fidl_sme::ClientSmeRequest::Scan{ txn, .. }) => {
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                let mut result = vec![scan_result.clone()];
                ctrl.send_on_result(&mut result.iter_mut()).expect("could not send scan result");
                ctrl.send_on_finished()
                    .expect("failed to send scan data");
            }
        );

        // Run the service.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Run the scan future to completion
        let expected_ap = convert_bss_info(&scan_result);
        assert_variant!(
            exec.run_until_stalled(&mut scan_fut),
            Poll::Ready(Ok(legacy::ScanResult {
                error: legacy::Error{ code: legacy::ErrCode::Ok, .. },
                aps: Some(aps)
            })) => {
                assert_eq!(aps.len(), 1);
                assert_eq!(aps[0], expected_ap);
            }
        );
    }

    #[test]
    fn scan_start_fails() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec, true, true, false);

        // Start the legacy service.
        let serve_fut =
            serve_legacy(test_values.legacy_stream, test_values.iface, test_values.saved_networks);
        pin_mut!(serve_fut);

        // Make a scan request.
        let mut scan_request = legacy::ScanRequest { timeout: TEST_SCAN_INTERVAL };
        let scan_fut = test_values.legacy_proxy.scan(&mut scan_request);
        pin_mut!(scan_fut);
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Run the service.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Run the scan future to completion
        assert_variant!(
            exec.run_until_stalled(&mut scan_fut),
            Poll::Ready(Ok(legacy::ScanResult {
                error: legacy::Error{ code: legacy::ErrCode::Internal, .. },
                aps: None,
            }))
        );
    }

    #[test]
    fn scan_results_fail() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec, true, true, true);

        // Start the legacy service.
        let serve_fut =
            serve_legacy(test_values.legacy_stream, test_values.iface, test_values.saved_networks);
        pin_mut!(serve_fut);

        // Make a scan request.
        let mut scan_request = legacy::ScanRequest { timeout: TEST_SCAN_INTERVAL };
        let scan_fut = test_values.legacy_proxy.scan(&mut scan_request);
        pin_mut!(scan_fut);
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Run the service.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Check the SME stream for a scan request and send an error.
        let sme_fut = test_values.sme_stream.into_future();
        pin_mut!(sme_fut);
        let sme_response = poll_sme_req(&mut exec, &mut sme_fut);
        assert_variant!(
            sme_response,
            Poll::Ready(fidl_sme::ClientSmeRequest::Scan{ txn, .. }) => {
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");

                let mut error = fidl_sme::ScanError {
                    code: fidl_sme::ScanErrorCode::NotSupported,
                    message: "".to_string(),
                };
                ctrl.send_on_error(&mut error).expect("could not send scan error");
            }
        );

        // Run the service.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Run the scan future to completion
        assert_variant!(
            exec.run_until_stalled(&mut scan_fut),
            Poll::Ready(Ok(legacy::ScanResult {
                error: legacy::Error{ code: legacy::ErrCode::NotSupported, .. },
                aps: None,
            }))
        );
    }
}
