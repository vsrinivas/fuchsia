// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use eapol;
use wlan_rsn::{self, rsna::UpdateSink, NegotiatedRsne};

// Trait has to be Send because wlanstack wraps SME into a Future
pub trait Authenticator: std::fmt::Debug + std::marker::Send {
    fn get_negotiated_rsne(&self) -> &NegotiatedRsne;
    fn reset(&mut self);
    fn initiate(&mut self, update_sink: &mut UpdateSink) -> Result<(), failure::Error>;
    fn on_eapol_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: &eapol::Frame,
    ) -> Result<(), failure::Error>;
}

impl Authenticator for wlan_rsn::Authenticator {
    fn get_negotiated_rsne(&self) -> &NegotiatedRsne {
        wlan_rsn::Authenticator::get_negotiated_rsne(self)
    }

    fn reset(&mut self) {
        wlan_rsn::Authenticator::reset(self)
    }

    fn initiate(&mut self, update_sink: &mut UpdateSink) -> Result<(), failure::Error> {
        wlan_rsn::Authenticator::initiate(self, update_sink)
    }

    fn on_eapol_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        frame: &eapol::Frame,
    ) -> Result<(), failure::Error> {
        wlan_rsn::Authenticator::on_eapol_frame(self, update_sink, frame)
    }
}
