// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod handshake;

use self::handshake::{
    fourway::{self, Fourway},
    group_key::{self, GroupKey},
};
use crate::integrity::integrity_algorithm;
use crate::key::gtk::Gtk;
use crate::key::ptk::Ptk;
use crate::rsna::{UpdateSink, VerifiedKeyFrame};
use crate::Error;
use failure;
use wlan_common::ie::rsn::akm::Akm;

#[derive(Debug, Clone, PartialEq)]
pub enum Key {
    Pmk(Vec<u8>),
    Ptk(Ptk),
    Gtk(Gtk),
    Igtk(Vec<u8>),
    MicRx(Vec<u8>),
    MicTx(Vec<u8>),
    Smk(Vec<u8>),
    Stk(Vec<u8>),
}

impl Key {
    pub fn name(&self) -> &'static str {
        match self {
            Key::Pmk(..) => "PMK",
            Key::Ptk(..) => "PTK",
            Key::Gtk(..) => "GTK",
            Key::Igtk(..) => "IGTK",
            Key::MicRx(..) => "MIC_RX",
            Key::MicTx(..) => "MIC_TX",
            Key::Smk(..) => "SMK",
            Key::Stk(..) => "STK",
        }
    }
}

#[derive(Debug, PartialEq)]
pub enum Method {
    FourWayHandshake(Fourway),
    GroupKeyHandshake(GroupKey),
}

impl Method {
    pub fn on_eapol_key_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        key_replay_counter: u64,
        frame: VerifiedKeyFrame,
    ) -> Result<(), failure::Error> {
        match self {
            Method::FourWayHandshake(hs) => {
                hs.on_eapol_key_frame(update_sink, key_replay_counter, frame)
            }
            Method::GroupKeyHandshake(hs) => {
                hs.on_eapol_key_frame(update_sink, key_replay_counter, frame)
            }
        }
    }
    pub fn initiate(
        &mut self,
        update_sink: &mut UpdateSink,
        key_replay_counter: u64,
    ) -> Result<(), failure::Error> {
        match self {
            Method::FourWayHandshake(hs) => hs.initiate(update_sink, key_replay_counter),
            // Only 4-Way Handshake supports initiation so far.
            _ => Ok(()),
        }
    }

    pub fn destroy(self) -> Config {
        match self {
            Method::FourWayHandshake(hs) => hs.destroy(),
            Method::GroupKeyHandshake(hs) => hs.destroy(),
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum Config {
    FourWayHandshake(fourway::Config),
    GroupKeyHandshake(group_key::Config),
}

/// Computes and returns a key frame's MIC.
/// Fails if the AKM has no associated integrity algorithm or MIC size, the given Key Frame's MIC
/// has a different size than the MIC length derived from the AKM or the Key Frame doesn't have its
/// MIC bit set.
pub fn compute_mic(kck: &[u8], akm: &Akm, frame: &eapol::KeyFrame) -> Result<Vec<u8>, Error> {
    let integrity_alg = integrity_algorithm(&akm).ok_or(Error::UnsupportedAkmSuite)?;
    let mic_len = akm.mic_bytes().ok_or(Error::UnsupportedAkmSuite)? as usize;
    if !frame.key_info.key_mic() {
        return Err(Error::ComputingMicForUnprotectedFrame);
    }
    if frame.key_mic.len() != mic_len {
        return Err(Error::MicSizesDiffer(frame.key_mic.len(), mic_len));
    }

    let mut buf = Vec::with_capacity(frame.len());
    frame.as_bytes(true, &mut buf);
    let written = buf.len();
    buf.truncate(written);
    let mut mic = integrity_alg.compute(kck, &buf[..])?;
    mic.truncate(mic_len);
    Ok(mic)
}

#[cfg(test)]
mod tests {
    use super::*;
    use bytes::Bytes;
    use wlan_common::ie::rsn::akm::PSK;

    fn fake_key_frame() -> eapol::KeyFrame {
        let mut key_info = eapol::KeyInformation(0);
        key_info.set_key_mic(true);
        eapol::KeyFrame { key_info, key_mic: Bytes::from(vec![0; 16]), ..Default::default() }
    }

    #[test]
    fn compute_mic_unknown_akm() {
        const KCK: [u8; 16] = [5; 16];
        let frame = fake_key_frame();
        let err = compute_mic(&KCK[..], &Akm::new_dot11(200), &frame)
            .expect_err("expected failure with unsupported AKM");
        match err {
            Error::UnsupportedAkmSuite => (),
            other => panic!("received unexpected error type: {}", other),
        }
    }

    #[test]
    fn compute_mic_bit_not_set() {
        const KCK: [u8; 16] = [5; 16];
        let frame = eapol::KeyFrame { key_info: eapol::KeyInformation(0), ..fake_key_frame() };
        let err = compute_mic(&KCK[..], &Akm::new_dot11(PSK), &frame)
            .expect_err("expected failure with MIC bit not being set");
        match err {
            Error::ComputingMicForUnprotectedFrame => (),
            other => panic!("received unexpected error type: {}", other),
        }
    }

    #[test]
    fn compute_mic_different_mic_sizes() {
        const KCK: [u8; 16] = [5; 16];
        let frame = eapol::KeyFrame { key_mic: Bytes::from(vec![]), ..fake_key_frame() };
        let err = compute_mic(&KCK[..], &Akm::new_dot11(PSK), &frame)
            .expect_err("expected failure with different MIC sizes");
        match err {
            Error::MicSizesDiffer(0, 16) => (),
            other => panic!("received unexpected error type: {}", other),
        }
    }

    #[test]
    fn compute_mic_success() {
        const KCK: [u8; 16] = [5; 16];
        let psk = Akm::new_dot11(PSK);
        let frame = fake_key_frame();
        let mic =
            compute_mic(&KCK[..], &psk, &frame).expect("expected failure with unsupported AKM");

        let integrity_alg =
            integrity_algorithm(&psk).expect("expected known integrity algorithm for PSK");
        let mut buf = vec![];
        frame.as_bytes(true, &mut buf);
        assert!(integrity_alg.verify(&KCK[..], &buf[..], &mic[..]));
    }

}
