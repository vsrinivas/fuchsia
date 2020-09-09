// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use eapol;
use std::sync::{Arc, Mutex};
use wlan_common::ie::rsn::rsne::RsnCapabilities;
use wlan_rsn::{rsna::UpdateSink, Error, NegotiatedProtection};

use crate::{ap::authenticator::Authenticator, test_utils};

#[derive(Debug)]
pub struct MockAuthenticator {
    initiate: Arc<Mutex<UpdateSink>>,
    on_eapol_frame: Arc<Mutex<UpdateSink>>,
    negotiated_protection: NegotiatedProtection,
}

impl Authenticator for MockAuthenticator {
    fn get_negotiated_protection(&self) -> &NegotiatedProtection {
        &self.negotiated_protection
    }

    fn reset(&mut self) {
        self.initiate.lock().unwrap().clear();
        self.on_eapol_frame.lock().unwrap().clear();
    }

    fn initiate(&mut self, update_sink: &mut UpdateSink) -> Result<(), Error> {
        update_sink.extend(self.initiate.lock().unwrap().drain(..));
        Ok(())
    }

    fn on_eapol_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        _frame: eapol::Frame<&[u8]>,
    ) -> Result<(), Error> {
        update_sink.extend(self.on_eapol_frame.lock().unwrap().drain(..));
        Ok(())
    }
}

impl MockAuthenticator {
    pub fn new(
        initiate_mock: Arc<Mutex<UpdateSink>>,
        on_eapol_frame_mock: Arc<Mutex<UpdateSink>>,
    ) -> Self {
        let rsne = test_utils::wpa2_psk_ccmp_rsne_with_caps(RsnCapabilities(0));
        MockAuthenticator {
            initiate: initiate_mock,
            on_eapol_frame: on_eapol_frame_mock,
            negotiated_protection: NegotiatedProtection::from_rsne(&rsne).unwrap(),
        }
    }
}
