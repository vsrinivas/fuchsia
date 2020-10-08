// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capabilities::{ClientCapabilities, StaCapabilities},
        client::{bss::BssInfo, rsn::Supplicant},
        test_utils::*,
        Ssid,
    },
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_mlme as fidl_mlme,
    futures::channel::mpsc,
    std::{
        convert::TryInto,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc, Mutex,
        },
    },
    wlan_common::{
        assert_variant,
        bss::Protection,
        channel,
        ie::{rsn::rsne::Rsne, write_wpa1_ie, *},
        mac,
    },
    wlan_rsn::{format_rsn_err, rsna::UpdateSink, Error},
    zerocopy::AsBytes,
};

fn fake_bss_description(ssid: Ssid, rsne: Option<Vec<u8>>) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription {
        bssid: [7, 1, 2, 77, 53, 8],
        ssid,
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        beacon_period: 100,
        dtim_period: 100,
        timestamp: 0,
        local_time: 0,
        cap: mac::CapabilityInfo(0).with_privacy(rsne.is_some()).0,
        rates: vec![0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c],
        country: None,
        rsne,
        vendor_ies: None,

        rcpi_dbmh: 0,
        rsni_dbh: 0,

        ht_cap: Some(Box::new(fidl_mlme::HtCapabilities { bytes: fake_ht_cap_bytes() })),
        ht_op: Some(Box::new(fidl_mlme::HtOperation { bytes: fake_ht_op_bytes() })),
        vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities { bytes: fake_vht_cap_bytes() })),
        vht_op: Some(Box::new(fidl_mlme::VhtOperation { bytes: fake_vht_op_bytes() })),
        chan: fidl_common::WlanChan { primary: 1, secondary80: 0, cbw: fidl_common::Cbw::Cbw40 },
        rssi_dbm: 0,
        snr_db: 0,
    }
}

pub fn fake_bss_with_bssid(ssid: Ssid, bssid: [u8; 6]) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription { bssid, ..fake_unprotected_bss_description(ssid) }
}

pub fn fake_bss_with_rates(ssid: Ssid, rates: Vec<u8>) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription { rates, ..fake_unprotected_bss_description(ssid) }
}

pub fn fake_bss_with_vendor_ies(ssid: Ssid, vendor_ies: Vec<u8>) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription {
        vendor_ies: Some(vendor_ies),
        ..fake_unprotected_bss_description(ssid)
    }
}

pub fn fake_unprotected_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    fake_bss_description(ssid, None)
}

pub fn fake_wep_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    let mut bss = fake_bss_description(ssid, None);
    bss.cap = mac::CapabilityInfo(bss.cap).with_privacy(true).0;
    bss
}

pub fn fake_wpa1_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    let mut bss = fake_bss_description(ssid, None);
    bss.cap = mac::CapabilityInfo(bss.cap).with_privacy(true).0;
    let mut vendor_ies = vec![];
    write_wpa1_ie(&mut vendor_ies, &make_wpa1_ie()).expect("failed to create wpa1 bss description");
    bss.vendor_ies = Some(vendor_ies);
    bss
}

pub fn fake_protected_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    let a_rsne = Rsne::wpa2_psk_ccmp_rsne();
    fake_bss_description(ssid, Some(a_rsne.into_bytes()))
}

pub fn fake_wpa3_mixed_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    let a_rsne = Rsne::wpa3_mixed_psk_ccmp_rsne();
    fake_bss_description(ssid, Some(a_rsne.into_bytes()))
}

pub fn fake_wpa3_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    let a_rsne = Rsne::wpa3_ccmp_rsne();
    fake_bss_description(ssid, Some(a_rsne.into_bytes()))
}

pub fn fake_vht_bss_description() -> fidl_mlme::BssDescription {
    let bss = fake_bss_description(vec![], None);
    fidl_mlme::BssDescription {
        chan: fake_chan(36),
        ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
            bytes: fake_ht_capabilities().as_bytes().try_into().unwrap(),
        })),
        ht_op: Some(Box::new(fidl_mlme::HtOperation {
            bytes: fake_ht_operation().as_bytes().try_into().unwrap(),
        })),
        vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
            bytes: fake_vht_capabilities().as_bytes().try_into().unwrap(),
        })),
        vht_op: Some(Box::new(fidl_mlme::VhtOperation {
            bytes: fake_vht_operation().as_bytes().try_into().unwrap(),
        })),
        ..bss
    }
}

pub fn fake_bss_info() -> BssInfo {
    BssInfo {
        bssid: [55, 11, 22, 3, 9, 70],
        ssid: b"foo".to_vec(),
        rx_dbm: 0,
        snr_db: 0,
        channel: 1,
        protection: Protection::Wpa2Personal,
        compatible: true,
        probe_resp_wsc: None,
        wmm_param: None,
    }
}

pub fn fake_chan(primary: u8) -> fidl_common::WlanChan {
    fidl_common::WlanChan { primary, cbw: fidl_common::Cbw::Cbw20, secondary80: 0 }
}

pub fn fake_scan_request() -> fidl_mlme::ScanRequest {
    fidl_mlme::ScanRequest {
        txn_id: 1,
        bss_type: fidl_mlme::BssTypes::Infrastructure,
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
        channel::Channel { primary: 1, cbw: channel::Cbw::Cbw40 },
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
            ht_cap: Some(Box::new(fidl_mlme::HtCapabilities { bytes: fake_ht_cap_bytes() })),
            vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities { bytes: fake_vht_cap_bytes() })),
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

pub fn mock_supplicant() -> (MockSupplicant, MockSupplicantController) {
    let started = Arc::new(AtomicBool::new(false));
    let start_failure = Arc::new(Mutex::new(None));
    let sink = Arc::new(Mutex::new(Ok(UpdateSink::default())));
    let on_eapol_frame_cb = Arc::new(Mutex::new(None));
    let supplicant = MockSupplicant {
        started: started.clone(),
        start_failure: start_failure.clone(),
        on_eapol_frame: sink.clone(),
        on_eapol_frame_cb: on_eapol_frame_cb.clone(),
    };
    let mock = MockSupplicantController {
        started,
        start_failure,
        mock_on_eapol_frame: sink,
        on_eapol_frame_cb,
    };
    (supplicant, mock)
}

type Cb = dyn Fn() + Send + 'static;

pub struct MockSupplicant {
    started: Arc<AtomicBool>,
    start_failure: Arc<Mutex<Option<anyhow::Error>>>,
    on_eapol_frame: Arc<Mutex<Result<UpdateSink, anyhow::Error>>>,
    on_eapol_frame_cb: Arc<Mutex<Option<Box<Cb>>>>,
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
        self.on_eapol_frame
            .lock()
            .unwrap()
            .as_mut()
            .map(|updates| {
                update_sink.extend(updates.drain(..));
            })
            .map_err(|e| format_rsn_err!("{:?}", e))
    }

    fn on_sae_handshake_ind(&mut self, _update_sink: &mut UpdateSink) -> Result<(), anyhow::Error> {
        unimplemented!()
    }
    fn on_sae_frame_rx(
        &mut self,
        _update_sink: &mut UpdateSink,
        _frame: fidl_mlme::SaeFrame,
    ) -> Result<(), anyhow::Error> {
        unimplemented!()
    }
    fn on_sae_timeout(
        &mut self,
        _update_sink: &mut UpdateSink,
        _event_id: u64,
    ) -> Result<(), anyhow::Error> {
        unimplemented!()
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
    on_eapol_frame_cb: Arc<Mutex<Option<Box<Cb>>>>,
}

impl MockSupplicantController {
    pub fn set_start_failure(&self, error: anyhow::Error) {
        self.start_failure.lock().unwrap().replace(error);
    }

    pub fn is_supplicant_started(&self) -> bool {
        self.started.load(Ordering::SeqCst)
    }

    pub fn set_on_eapol_frame_results(&self, updates: UpdateSink) {
        *self.mock_on_eapol_frame.lock().unwrap() = Ok(updates);
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
