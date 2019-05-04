// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, format_err};
use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc, Mutex,
};
use wlan_common::ie::rsn::rsne::RsnCapabilities;
use wlan_rsn::rsna::UpdateSink;

use crate::{
    client::rsn::Supplicant,
    test_utils::{self, *},
    InfoEvent, InfoStream, Ssid,
};

fn fake_bss_description(ssid: Ssid, rsn: Option<Vec<u8>>) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription {
        bssid: [0, 0, 0, 0, 0, 0],
        ssid,
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        beacon_period: 100,
        dtim_period: 100,
        timestamp: 0,
        local_time: 0,
        cap: fidl_mlme::CapabilityInfo {
            ess: false,
            ibss: false,
            cf_pollable: false,
            cf_poll_req: false,
            privacy: false,
            short_preamble: false,
            spectrum_mgmt: false,
            qos: false,
            short_slot_time: false,
            apsd: false,
            radio_msmt: false,
            delayed_block_ack: false,
            immediate_block_ack: false,
        },
        basic_rate_set: vec![],
        op_rate_set: vec![],
        country: None,
        rsn,

        rcpi_dbmh: 0,
        rsni_dbh: 0,

        ht_cap: None,
        ht_op: None,
        vht_cap: None,
        vht_op: None,
        chan: fidl_common::WlanChan { primary: 1, secondary80: 0, cbw: fidl_common::Cbw::Cbw20 },
        rssi_dbm: 0,
    }
}

pub fn fake_bss_with_bssid(ssid: Ssid, bssid: [u8; 6]) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription { bssid, ..fake_unprotected_bss_description(ssid) }
}

pub fn fake_unprotected_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    fake_bss_description(ssid, None)
}

pub fn fake_wep_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    let mut bss = fake_bss_description(ssid, None);
    bss.cap.privacy = true;
    bss
}

pub fn fake_protected_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    let a_rsne = test_utils::wpa2_psk_ccmp_rsne_with_caps(RsnCapabilities(0));
    fake_bss_description(ssid, Some(test_utils::rsne_as_bytes(a_rsne)))
}

pub fn fake_vht_bss_description() -> fidl_mlme::BssDescription {
    let bss = fake_bss_description(vec![], None);
    fidl_mlme::BssDescription {
        chan: fake_chan(36),
        ht_cap: Some(Box::new(fake_ht_capabilities())),
        ht_op: Some(Box::new(fake_ht_operation())),
        vht_cap: Some(Box::new(fake_vht_capabilities())),
        vht_op: Some(Box::new(fake_vht_operation())),
        ..bss
    }
}

pub fn fake_chan(primary: u8) -> fidl_common::WlanChan {
    fidl_common::WlanChan { primary, cbw: fidl_common::Cbw::Cbw20, secondary80: 0 }
}

pub fn expect_info_event(info_stream: &mut InfoStream, expected_event: InfoEvent) {
    if let Ok(Some(e)) = info_stream.try_next() {
        assert_eq!(e, expected_event);
    } else {
        panic!("expect event to InfoSink");
    }
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

type Cb = Fn() + Send + 'static;

pub struct MockSupplicant {
    started: Arc<AtomicBool>,
    start_failure: Arc<Mutex<Option<failure::Error>>>,
    on_eapol_frame: Arc<Mutex<Result<UpdateSink, failure::Error>>>,
    on_eapol_frame_cb: Arc<Mutex<Option<Box<Cb>>>>,
}

impl Supplicant for MockSupplicant {
    fn start(&mut self) -> Result<(), failure::Error> {
        match &*self.start_failure.lock().unwrap() {
            Some(error) => bail!("{:?}", error),
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
        _frame: &eapol::Frame,
    ) -> Result<(), failure::Error> {
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
            .map_err(|e| format_err!("{:?}", e))
    }
}

impl std::fmt::Debug for MockSupplicant {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "MockSupplicant cannot be formatted")
    }
}

pub struct MockSupplicantController {
    started: Arc<AtomicBool>,
    start_failure: Arc<Mutex<Option<failure::Error>>>,
    mock_on_eapol_frame: Arc<Mutex<Result<UpdateSink, failure::Error>>>,
    on_eapol_frame_cb: Arc<Mutex<Option<Box<Cb>>>>,
}

impl MockSupplicantController {
    pub fn set_start_failure(&self, error: failure::Error) {
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

    pub fn set_on_eapol_frame_failure(&self, error: failure::Error) {
        *self.mock_on_eapol_frame.lock().unwrap() = Err(error);
    }
}
