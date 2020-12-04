// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capabilities::{ClientCapabilities, StaCapabilities},
        client::{bss::BssInfo, rsn::Supplicant},
        Ssid,
    },
    fidl_fuchsia_wlan_internal as fidl_internal, fidl_fuchsia_wlan_mlme as fidl_mlme,
    fuchsia_zircon as zx,
    futures::channel::mpsc,
    std::sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
    wlan_common::{
        assert_variant,
        bss::Protection,
        channel,
        ie::{
            fake_ies::{fake_ht_cap_bytes, fake_vht_cap_bytes},
            *,
        },
    },
    wlan_rsn::{auth, format_rsn_err, psk, rsna::UpdateSink, Error},
};

pub fn fake_bss_info() -> BssInfo {
    BssInfo {
        bssid: [55, 11, 22, 3, 9, 70],
        ssid: Ssid::from(b"foo".clone()),
        rssi_dbm: 0,
        snr_db: 0,
        signal_report_time: zx::Time::ZERO,
        channel: channel::Channel { primary: 1, cbw: channel::Cbw::Cbw20 },
        protection: Protection::Wpa2Personal,
        compatible: true,
        ht_cap: Some(fidl_internal::HtCapabilities { bytes: fake_ht_cap_bytes() }),
        vht_cap: Some(fidl_internal::VhtCapabilities { bytes: fake_vht_cap_bytes() }),
        probe_resp_wsc: None,
        wmm_param: None,
        bss_desc: None,
    }
}

pub fn fake_scan_request() -> fidl_mlme::ScanRequest {
    fidl_mlme::ScanRequest {
        txn_id: 1,
        bss_type: fidl_internal::BssTypes::Infrastructure,
        bssid: [8, 2, 6, 2, 1, 11],
        ssid: vec![],
        scan_type: fidl_mlme::ScanTypes::Active,
        probe_delay: 5,
        channel_list: Some(vec![11]),
        min_channel_time: 50,
        max_channel_time: 50,
        ssid_list: None,
    }
}

pub fn fake_wmm_param() -> fidl_mlme::WmmParameter {
    #[rustfmt::skip]
    let wmm_param = fidl_mlme::WmmParameter {
        bytes: [
            0x80, // Qos Info - U-ASPD enabled
            0x00, // reserved
            0x03, 0xa4, 0x00, 0x00, // Best effort AC params
            0x27, 0xa4, 0x00, 0x00, // Background AC params
            0x42, 0x43, 0x5e, 0x00, // Video AC params
            0x62, 0x32, 0x2f, 0x00, // Voice AC params
        ]
    };
    wmm_param
}

pub fn create_join_conf(result_code: fidl_mlme::JoinResultCodes) -> fidl_mlme::MlmeEvent {
    fidl_mlme::MlmeEvent::JoinConf { resp: fidl_mlme::JoinConfirm { result_code } }
}

pub fn create_auth_conf(
    bssid: [u8; 6],
    result_code: fidl_mlme::AuthenticateResultCodes,
) -> fidl_mlme::MlmeEvent {
    fidl_mlme::MlmeEvent::AuthenticateConf {
        resp: fidl_mlme::AuthenticateConfirm {
            peer_sta_address: bssid,
            auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
            result_code,
        },
    }
}

pub fn fake_negotiated_channel_and_capabilities() -> (channel::Channel, ClientCapabilities) {
    // Based on fake_bss_description, device_info and create_assoc_conf
    let mut ht_cap = fake_ht_capabilities();
    // Fuchsia does not support tx_stbc yet.
    ht_cap.ht_cap_info = ht_cap.ht_cap_info.with_tx_stbc(false);
    (
        channel::Channel { primary: 3, cbw: channel::Cbw::Cbw40 },
        ClientCapabilities(StaCapabilities {
            cap_info: crate::test_utils::fake_capability_info(),
            rates: [0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c]
                .iter()
                .cloned()
                .map(SupportedRate)
                .collect(),
            ht_cap: Some(ht_cap),
            vht_cap: Some(fake_vht_capabilities()),
        }),
    )
}

pub fn create_assoc_conf(result_code: fidl_mlme::AssociateResultCodes) -> fidl_mlme::MlmeEvent {
    fidl_mlme::MlmeEvent::AssociateConf {
        resp: fidl_mlme::AssociateConfirm {
            result_code,
            association_id: 55,
            cap_info: crate::test_utils::fake_capability_info().raw(),
            rates: vec![0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c],
            // TODO(fxbug.dev/43938): mock with fake WMM param
            wmm_param: None,
            ht_cap: Some(Box::new(fidl_internal::HtCapabilities { bytes: fake_ht_cap_bytes() })),
            vht_cap: Some(Box::new(fidl_internal::VhtCapabilities { bytes: fake_vht_cap_bytes() })),
        },
    }
}

pub fn expect_stream_empty<T>(stream: &mut mpsc::UnboundedReceiver<T>, error_msg: &str) {
    assert_variant!(
        stream.try_next(),
        Ok(None) | Err(..),
        format!("error, receiver not empty: {}", error_msg)
    );
}

fn mock_supplicant(auth_cfg: auth::Config) -> (MockSupplicant, MockSupplicantController) {
    let started = Arc::new(AtomicBool::new(false));
    let start_failure = Arc::new(Mutex::new(None));
    let on_eapol_frame_sink = Arc::new(Mutex::new(Ok(UpdateSink::default())));
    let on_sae_handshake_ind_sink = Arc::new(Mutex::new(Ok(UpdateSink::default())));
    let on_sae_frame_rx_sink = Arc::new(Mutex::new(Ok(UpdateSink::default())));
    let on_sae_timeout_sink = Arc::new(Mutex::new(Ok(UpdateSink::default())));
    let on_eapol_frame_cb = Arc::new(Mutex::new(None));
    let supplicant = MockSupplicant {
        started: started.clone(),
        start_failure: start_failure.clone(),
        on_eapol_frame: on_eapol_frame_sink.clone(),
        on_eapol_frame_cb: on_eapol_frame_cb.clone(),
        on_sae_handshake_ind: on_sae_handshake_ind_sink.clone(),
        on_sae_frame_rx: on_sae_frame_rx_sink.clone(),
        on_sae_timeout: on_sae_timeout_sink.clone(),
        auth_cfg,
    };
    let mock = MockSupplicantController {
        started,
        start_failure,
        mock_on_eapol_frame: on_eapol_frame_sink,
        mock_on_sae_handshake_ind: on_sae_handshake_ind_sink,
        mock_on_sae_frame_rx: on_sae_frame_rx_sink,
        mock_on_sae_timeout: on_sae_timeout_sink,
        on_eapol_frame_cb,
    };
    (supplicant, mock)
}

const MOCK_PASS: &str = "dummy_password";
const MOCK_SSID: &str = "network_ssid";

pub fn mock_psk_supplicant() -> (MockSupplicant, MockSupplicantController) {
    let config = auth::Config::ComputedPsk(
        psk::compute(MOCK_PASS.as_bytes(), MOCK_SSID.as_bytes())
            .expect("Failed to create mock psk"),
    );
    mock_supplicant(config)
}

pub fn mock_sae_supplicant() -> (MockSupplicant, MockSupplicantController) {
    let config = auth::Config::Sae {
        password: MOCK_PASS.as_bytes().to_vec(),
        mac: [0xaa; 6],
        peer_mac: [0xbb; 6],
    };
    mock_supplicant(config)
}

type Cb = dyn Fn() + Send + 'static;

pub struct MockSupplicant {
    started: Arc<AtomicBool>,
    start_failure: Arc<Mutex<Option<anyhow::Error>>>,
    on_eapol_frame: Arc<Mutex<Result<UpdateSink, anyhow::Error>>>,
    on_eapol_frame_cb: Arc<Mutex<Option<Box<Cb>>>>,
    on_sae_handshake_ind: Arc<Mutex<Result<UpdateSink, anyhow::Error>>>,
    on_sae_frame_rx: Arc<Mutex<Result<UpdateSink, anyhow::Error>>>,
    on_sae_timeout: Arc<Mutex<Result<UpdateSink, anyhow::Error>>>,
    auth_cfg: auth::Config,
}

fn populate_update_sink(
    update_sink: &mut UpdateSink,
    results: &Arc<Mutex<Result<UpdateSink, anyhow::Error>>>,
) -> Result<(), Error> {
    results
        .lock()
        .unwrap()
        .as_mut()
        .map(|updates| {
            update_sink.extend(updates.drain(..));
        })
        .map_err(|e| format_rsn_err!("{:?}", e))
}

impl Supplicant for MockSupplicant {
    fn start(&mut self) -> Result<(), Error> {
        match &*self.start_failure.lock().unwrap() {
            Some(error) => return Err(format_rsn_err!("{:?}", error)),
            None => {
                self.started.store(true, Ordering::SeqCst);
                Ok(())
            }
        }
    }

    fn reset(&mut self) {
        let _ = self.on_eapol_frame.lock().unwrap().as_mut().map(|updates| updates.clear());
    }

    fn on_eapol_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        _frame: eapol::Frame<&[u8]>,
    ) -> Result<(), Error> {
        if let Some(cb) = self.on_eapol_frame_cb.lock().unwrap().as_mut() {
            cb();
        }
        populate_update_sink(update_sink, &self.on_eapol_frame)
    }

    fn on_pmk_available(
        &mut self,
        _update_sink: &mut UpdateSink,
        _pmk: &[u8],
        _pmkid: &[u8],
    ) -> Result<(), anyhow::Error> {
        unimplemented!()
    }

    fn on_sae_handshake_ind(&mut self, update_sink: &mut UpdateSink) -> Result<(), anyhow::Error> {
        populate_update_sink(update_sink, &self.on_sae_handshake_ind).map_err(|e| e.into())
    }
    fn on_sae_frame_rx(
        &mut self,
        update_sink: &mut UpdateSink,
        _frame: fidl_mlme::SaeFrame,
    ) -> Result<(), anyhow::Error> {
        populate_update_sink(update_sink, &self.on_sae_frame_rx).map_err(|e| e.into())
    }
    fn on_sae_timeout(
        &mut self,
        update_sink: &mut UpdateSink,
        _event_id: u64,
    ) -> Result<(), anyhow::Error> {
        populate_update_sink(update_sink, &self.on_sae_timeout).map_err(|e| e.into())
    }
    fn get_auth_cfg(&self) -> &auth::Config {
        &self.auth_cfg
    }
    fn get_auth_method(&self) -> auth::MethodName {
        self.auth_cfg.method_name()
    }
}

impl std::fmt::Debug for MockSupplicant {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "MockSupplicant cannot be formatted")
    }
}

pub struct MockSupplicantController {
    started: Arc<AtomicBool>,
    start_failure: Arc<Mutex<Option<anyhow::Error>>>,
    mock_on_eapol_frame: Arc<Mutex<Result<UpdateSink, anyhow::Error>>>,
    mock_on_sae_handshake_ind: Arc<Mutex<Result<UpdateSink, anyhow::Error>>>,
    mock_on_sae_frame_rx: Arc<Mutex<Result<UpdateSink, anyhow::Error>>>,
    mock_on_sae_timeout: Arc<Mutex<Result<UpdateSink, anyhow::Error>>>,
    on_eapol_frame_cb: Arc<Mutex<Option<Box<Cb>>>>,
}

impl MockSupplicantController {
    pub fn set_start_failure(&self, error: anyhow::Error) {
        self.start_failure.lock().unwrap().replace(error);
    }

    pub fn is_supplicant_started(&self) -> bool {
        self.started.load(Ordering::SeqCst)
    }

    pub fn set_on_eapol_frame_updates(&self, updates: UpdateSink) {
        *self.mock_on_eapol_frame.lock().unwrap() = Ok(updates);
    }

    pub fn set_on_sae_handshake_ind_updates(&self, updates: UpdateSink) {
        *self.mock_on_sae_handshake_ind.lock().unwrap() = Ok(updates);
    }

    pub fn set_on_sae_frame_rx_updates(&self, updates: UpdateSink) {
        *self.mock_on_sae_frame_rx.lock().unwrap() = Ok(updates);
    }

    pub fn set_on_sae_timeout_updates(&self, updates: UpdateSink) {
        *self.mock_on_sae_timeout.lock().unwrap() = Ok(updates);
    }

    pub fn set_on_sae_timeout_failure(&self, error: anyhow::Error) {
        *self.mock_on_sae_timeout.lock().unwrap() = Err(error);
    }

    pub fn set_on_eapol_frame_callback<F>(&self, cb: F)
    where
        F: Fn() + Send + 'static,
    {
        *self.on_eapol_frame_cb.lock().unwrap() = Some(Box::new(cb));
    }

    pub fn set_on_eapol_frame_failure(&self, error: anyhow::Error) {
        *self.mock_on_eapol_frame.lock().unwrap() = Err(error);
    }
}
