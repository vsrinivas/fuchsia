// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ie::rsn::{
    akm::{Akm, PSK, SAE},
    cipher::{Cipher, BIP_CMAC_128, CCMP_128, TKIP},
    rsne::{RsnCapabilities, Rsne},
};

pub fn fake_wpa2_a_rsne() -> Rsne {
    let mut rsne = Rsne::new();
    rsne.group_data_cipher_suite = Some(Cipher::new_dot11(CCMP_128));
    rsne.pairwise_cipher_suites.push(Cipher::new_dot11(CCMP_128));
    rsne.pairwise_cipher_suites.push(Cipher::new_dot11(TKIP));
    rsne.akm_suites.push(Akm::new_dot11(PSK));
    rsne
}

pub fn fake_wpa2_s_rsne() -> Rsne {
    let mut rsne = Rsne::new();
    rsne.group_data_cipher_suite = Some(Cipher::new_dot11(CCMP_128));
    rsne.pairwise_cipher_suites.push(Cipher::new_dot11(CCMP_128));
    rsne.akm_suites.push(Akm::new_dot11(PSK));
    rsne
}

pub fn fake_wpa3_rsne() -> Rsne {
    let mut rsne = Rsne::new();
    rsne.group_data_cipher_suite = Some(Cipher::new_dot11(CCMP_128));
    rsne.pairwise_cipher_suites.push(Cipher::new_dot11(CCMP_128));
    rsne.akm_suites.push(Akm::new_dot11(SAE));
    rsne.group_mgmt_cipher_suite = Some(Cipher::new_dot11(BIP_CMAC_128));
    let mut caps = RsnCapabilities(0);
    caps.set_mgmt_frame_protection_cap(true);
    caps.set_mgmt_frame_protection_req(true);
    rsne.rsn_capabilities = Some(caps);
    rsne
}
